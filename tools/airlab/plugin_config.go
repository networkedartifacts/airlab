package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/256dpi/naos/pkg/msg"
	"github.com/spf13/cobra"
	"gopkg.in/yaml.v3"

	"github.com/networkedartifacts/airlab/tools/alp"
)

var pluginConfigCmd = &cobra.Command{
	Use:   "config [key=value...]",
	Short: "Manage config values on a device",
	Long:  "Show, set or delete config values for a plugin. Without arguments, the current config is shown.",
	RunE: func(cmd *cobra.Command, args []string) error {
		dir, _ := cmd.Flags().GetString("dir")
		device, _ := cmd.Flags().GetString("device")
		del, _ := cmd.Flags().GetBool("delete")
		return pluginConfig(dir, args, device, del)
	},
}

func init() {
	pluginConfigCmd.Flags().StringP("dir", "C", ".", "Plugin directory containing alp.yml.")
	pluginConfigCmd.Flags().StringP("device", "d", "", "Serial device path.")
	pluginConfigCmd.Flags().Bool("delete", false, "Delete the config file from the device.")
	pluginCmd.AddCommand(pluginConfigCmd)
}

func pluginConfig(dir string, pairs []string, device string, del bool) error {
	// determine root
	root, err := filepath.Abs(dir)
	if err != nil {
		return err
	}

	// load manifest
	manifestRaw, err := os.ReadFile(filepath.Join(root, "alp.yml"))
	if err != nil {
		return err
	}

	// parse manifest
	var manifest pluginManifest
	err = yaml.Unmarshal(manifestRaw, &manifest)
	if err != nil {
		return err
	}

	// check name
	if manifest.Name == "" {
		return fmt.Errorf("missing name in manifest")
	}

	// print info
	fmt.Printf("==> Plugin: %s\n", manifest.Name)

	// open device
	man, err := filesOpenDevice(device)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// determine remote file
	fileName := "/int/engine/" + manifest.Name + ".alc"

	// handle delete
	if del {
		fmt.Printf("==> Deleting: %s\n", fileName)
		err = man.UseSession(func(s *msg.Session) error {
			return msg.RemovePath(s, fileName, time.Second*5)
		})
		if err != nil {
			return err
		}
		fmt.Printf("==> Done!\n")
		return nil
	}

	// get main config
	mainConfig, ok := manifest.Config["main"]
	if !ok || len(mainConfig.Sections) == 0 {
		return fmt.Errorf("no config defined in manifest")
	}

	// build type map from schema
	typeMap := map[string]alp.ConfigType{}
	for _, section := range mainConfig.Sections {
		for _, item := range section.Items {
			typeMap[item.Key] = item.Type
		}
	}

	// parse key=value pairs
	values := map[string]any{}
	for _, pair := range pairs {
		parts := strings.SplitN(pair, "=", 2)
		if len(parts) != 2 {
			return fmt.Errorf("invalid pair: %q (expected key=value)", pair)
		}
		key, raw := parts[0], parts[1]

		// lookup type
		typ, ok := typeMap[key]
		if !ok {
			return fmt.Errorf("unknown config key: %q", key)
		}

		// parse value
		val, err := parseConfigValue(typ, raw)
		if err != nil {
			return fmt.Errorf("key %q: %w", key, err)
		}

		values[key] = val
	}

	// handle show (no pairs given)
	if len(values) == 0 {
		return pluginConfigShow(man, fileName, typeMap)
	}

	// handle set
	return pluginConfigSet(man, fileName, typeMap, values)
}

func pluginConfigShow(man *msg.ManagedDevice, fileName string, typeMap map[string]alp.ConfigType) error {
	// download existing config
	var data []byte
	err := man.UseSession(func(s *msg.Session) error {
		var err error
		data, err = msg.ReadFile(s, fileName, nil, time.Minute)
		return err
	})
	if err != nil {
		return fmt.Errorf("no config found: %w", err)
	}

	// decode bundle
	bundle, err := alp.DecodeBundle(data)
	if err != nil {
		return err
	}

	// print values
	fmt.Printf("==> Config: %s\n", fileName)
	for _, sec := range bundle.Sections {
		if sec.Type != alp.BundleTypeBinary || len(sec.Data) < 2 {
			continue
		}

		// read type byte and decode value
		typ := alp.ConfigTypeFromByte(sec.Data[0])
		if typ == "" {
			continue
		}
		val, _ := alp.DecodeConfigValue(typ, sec.Data[1:])
		if val == nil {
			continue
		}

		fmt.Printf("  %s = %v\n", sec.Name, val)
	}

	return nil
}

func pluginConfigSet(man *msg.ManagedDevice, fileName string, typeMap map[string]alp.ConfigType, values map[string]any) error {
	// download existing config
	var existing *alp.Bundle
	err := man.UseSession(func(s *msg.Session) error {
		data, err := msg.ReadFile(s, fileName, nil, time.Minute)
		if err != nil {
			return nil // ignore errors (file may not exist)
		}
		existing, err = alp.DecodeBundle(data)
		if err != nil {
			return nil // ignore decode errors
		}
		return nil
	})
	if err != nil {
		return err
	}

	// merge: start with existing values, override with new ones
	merged := map[string]alp.BundleSection{}
	if existing != nil {
		fmt.Printf("==> Existing: %d values\n", len(existing.Sections))
		for _, sec := range existing.Sections {
			if sec.Type == alp.BundleTypeBinary {
				merged[sec.Name] = sec
			}
		}
	}

	// apply new values
	for key, val := range values {
		typ := typeMap[key]
		var data []byte
		data = append(data, alp.ConfigValueTypeByte(typ))
		data = append(data, alp.EncodeConfigValue(typ, val)...)
		merged[key] = alp.BundleSection{
			Type: alp.BundleTypeBinary,
			Name: key,
			Data: data,
		}
	}

	// build bundle
	var bundle alp.Bundle
	for _, sec := range merged {
		bundle.Sections = append(bundle.Sections, sec)
	}

	// encode bundle
	bundleData := bundle.Encode()

	// print values
	fmt.Printf("==> Writing: %d values (%d bytes)\n", len(merged), len(bundleData))
	for key, val := range values {
		fmt.Printf("  %s = %v\n", key, val)
	}

	// upload config
	fmt.Printf("==> Uploading: %s\n", fileName)
	err = man.UseSession(func(s *msg.Session) error {
		return msg.WriteFile(s, fileName, bundleData, func(u uint32) {}, time.Minute)
	})
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Done!\n")

	return nil
}

func parseConfigValue(typ alp.ConfigType, raw string) (any, error) {
	switch typ {
	case alp.ConfigTypeString:
		return raw, nil
	case alp.ConfigTypeBool:
		switch strings.ToLower(raw) {
		case "true", "1", "yes":
			return true, nil
		case "false", "0", "no":
			return false, nil
		default:
			return nil, fmt.Errorf("invalid bool value: %q", raw)
		}
	case alp.ConfigTypeInt:
		v, err := strconv.Atoi(raw)
		if err != nil {
			return nil, fmt.Errorf("invalid int value: %q", raw)
		}
		return v, nil
	case alp.ConfigTypeFloat:
		v, err := strconv.ParseFloat(raw, 64)
		if err != nil {
			return nil, fmt.Errorf("invalid float value: %q", raw)
		}
		return v, nil
	default:
		return nil, fmt.Errorf("unknown type: %q", typ)
	}
}
