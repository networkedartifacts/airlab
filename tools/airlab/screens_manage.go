package main

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/256dpi/naos/pkg/msg"
	"github.com/spf13/cobra"

	"github.com/networkedartifacts/airlab/tools/alp"
)

var screensShowCmd = &cobra.Command{
	Use:   "show",
	Short: "Show idle screen configuration",
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		return screensShow()
	},
}

var screensSetCmd = &cobra.Command{
	Use:   "set <id> <plugin> [key=value...]",
	Short: "Set an idle screen entry",
	Args:  cobra.MinimumNArgs(2),
	RunE: func(cmd *cobra.Command, args []string) error {
		return screensSet(args[0], args[1], args[2:])
	},
}

var screensClearCmd = &cobra.Command{
	Use:   "clear <id>",
	Short: "Clear an idle screen entry",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		return screensClear(args[0])
	},
}

func init() {
	screensCmd.AddCommand(screensShowCmd)
	screensCmd.AddCommand(screensSetCmd)
	screensCmd.AddCommand(screensClearCmd)
}

const screensFile = "/int/config/screens.alb"

func screensShow() error {
	// open device
	man, err := filesOpenDevice(screensDevice)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// download existing bundle
	bundle, err := screensDownload(man)
	if err != nil {
		return err
	}

	// collect configs by id
	configs := map[string]*alp.Bundle{}
	for _, sec := range bundle.Sections {
		if sec.Type == alp.BundleTypeConfig {
			cb, err := alp.DecodeBundle(sec.Data)
			if err == nil {
				configs[sec.Name] = cb
			}
		}
	}

	// collect screens in order
	type screen struct {
		id     string
		plugin string
	}
	var screens []screen
	for _, sec := range bundle.Sections {
		if sec.Type == alp.BundleTypeAttr {
			screens = append(screens, screen{id: sec.Name, plugin: string(sec.Data)})
		}
	}

	// print entries
	if len(screens) == 0 {
		fmt.Printf("==> No screens configured.\n")
		return nil
	}
	fmt.Printf("==> Screens: %d\n", len(screens))
	for i, s := range screens {
		fmt.Printf("  %d: %s (%s)\n", i, s.id, s.plugin)
		if cb, ok := configs[s.id]; ok {
			for _, sec := range cb.Sections {
				if sec.Type != alp.BundleTypeBinary || len(sec.Data) < 2 {
					continue
				}
				typ := alp.ConfigTypeFromByte(sec.Data[0])
				if typ == "" {
					continue
				}
				val, _ := alp.DecodeConfigValue(typ, sec.Data[1:])
				if val == nil {
					continue
				}
				fmt.Printf("    %s = %v\n", sec.Name, val)
			}
		}
	}

	return nil
}

func screensSet(id, plugin string, pairs []string) error {
	// open device
	man, err := filesOpenDevice(screensDevice)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// download existing bundle
	bundle, err := screensDownload(man)
	if err != nil {
		return err
	}

	// remove existing sections with matching id
	screensRemoveSections(bundle, id)

	// add attr section
	bundle.Sections = append(bundle.Sections, alp.BundleSection{
		Type: alp.BundleTypeAttr,
		Name: id,
		Data: []byte(plugin),
	})

	// add config section if pairs given
	if len(pairs) > 0 {
		var config alp.Bundle
		for _, pair := range pairs {
			parts := strings.SplitN(pair, "=", 2)
			if len(parts) != 2 {
				return fmt.Errorf("invalid pair: %q (expected key=value)", pair)
			}
			key, raw := parts[0], parts[1]
			typ, val := screensInferValue(raw)
			var data []byte
			data = append(data, alp.ConfigValueTypeByte(typ))
			data = append(data, alp.EncodeConfigValue(typ, val)...)
			config.Sections = append(config.Sections, alp.BundleSection{
				Type: alp.BundleTypeBinary,
				Name: key,
				Data: data,
			})
		}
		bundle.Sections = append(bundle.Sections, alp.BundleSection{
			Type: alp.BundleTypeConfig,
			Name: id,
			Data: config.Encode(),
		})
	}

	// upload bundle
	err = screensUpload(man, bundle)
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Set: %s (%s)\n", id, plugin)

	return nil
}

func screensClear(id string) error {
	// open device
	man, err := filesOpenDevice(screensDevice)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// download existing bundle
	bundle, err := screensDownload(man)
	if err != nil {
		return err
	}

	// remove sections with matching id
	screensRemoveSections(bundle, id)

	// upload bundle
	err = screensUpload(man, bundle)
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Cleared: %s\n", id)

	return nil
}

func screensDownload(man *msg.ManagedDevice) (*alp.Bundle, error) {
	// download existing bundle
	var bundle *alp.Bundle
	err := man.UseSession(func(s *msg.Session) error {
		data, err := msg.ReadFile(s, screensFile, nil, time.Minute)
		if err != nil {
			return nil // ignore error (file may not exist)
		}
		bundle, err = alp.DecodeBundle(data)
		if err != nil {
			return nil // ignore decode error
		}
		return nil
	})
	if err != nil {
		return nil, err
	}

	// ensure bundle
	if bundle == nil {
		bundle = &alp.Bundle{}
	}

	return bundle, nil
}

func screensUpload(man *msg.ManagedDevice, bundle *alp.Bundle) error {
	// encode bundle
	data := bundle.Encode()

	// log
	fmt.Printf("==> Uploading: %s (%d bytes)\n", screensFile, len(data))

	// upload bundle
	err := man.UseSession(func(s *msg.Session) error {
		err := msg.MakePath(s, "/int/config", time.Second)
		if err != nil {
			return err
		}
		return msg.WriteFile(s, screensFile, data, nil, time.Minute)
	})
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Done!\n")

	return nil
}

func screensInferValue(raw string) (alp.ConfigType, any) {
	// try bool
	switch strings.ToLower(raw) {
	case "true":
		return alp.ConfigTypeBool, true
	case "false":
		return alp.ConfigTypeBool, false
	}

	// try int
	if v, err := strconv.Atoi(raw); err == nil {
		return alp.ConfigTypeInt, v
	}

	// try float
	if v, err := strconv.ParseFloat(raw, 64); err == nil {
		return alp.ConfigTypeFloat, v
	}

	// default to string
	return alp.ConfigTypeString, raw
}

func screensRemoveSections(bundle *alp.Bundle, id string) {
	var filtered []alp.BundleSection
	for _, sec := range bundle.Sections {
		if sec.Name == id && (sec.Type == alp.BundleTypeAttr || sec.Type == alp.BundleTypeConfig) {
			continue
		}
		filtered = append(filtered, sec)
	}
	bundle.Sections = filtered
}
