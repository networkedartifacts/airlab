package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/samber/lo"
	"github.com/spf13/cobra"
	"golang.org/x/mod/semver"
	"gopkg.in/yaml.v3"

	"github.com/networkedartifacts/airlab/tools/alp"
)

type pluginManifest struct {
	Name    string                `yaml:"name"`
	Title   string                `yaml:"title"`
	Version string                `yaml:"version"`
	Binary  map[string]string     `yaml:"binary"`
	Sprites []string              `yaml:"sprites"`
	Config  map[string]alp.Config `yaml:"config"`
}

var pluginBundleCmd = &cobra.Command{
	Use:   "bundle <dir> <output>",
	Short: "Bundle a plugin directory into a bundle file",
	Args:  cobra.ExactArgs(2),
	RunE: func(cmd *cobra.Command, args []string) error {
		return pluginBundle(args[0], args[1])
	},
}

func init() {
	pluginCmd.AddCommand(pluginBundleCmd)
}

func pluginBundle(dir, out string) error {
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

	// check fields
	if manifest.Name == "" {
		return fmt.Errorf("missing name")
	} else if manifest.Title == "" {
		return fmt.Errorf("missing title")
	} else if manifest.Version == "" {
		return fmt.Errorf("missing version")
	} else if !semver.IsValid(manifest.Version) {
		return fmt.Errorf("invalid version")
	}

	// validate binary keys
	for key := range manifest.Binary {
		if key != "main" {
			return fmt.Errorf("unsupported binary key: %q (only \"main\" is supported)", key)
		}
	}

	// validate config keys
	for key := range manifest.Config {
		if key != "main" {
			return fmt.Errorf("unsupported config key: %q (only \"main\" is supported)", key)
		}
	}

	// resolves sprites
	var sprites []string
	for _, sprite := range manifest.Sprites {
		matches, err := filepath.Glob(filepath.Join(root, sprite))
		if err != nil {
			return err
		}
		sprites = append(sprites, matches...)
	}

	// print fields
	fmt.Printf("==> Name: %s\n", manifest.Name)
	fmt.Printf("==> Title: %s\n", manifest.Title)
	fmt.Printf("==> Version: %s\n", manifest.Version)

	// print binaries
	for key, bin := range manifest.Binary {
		fmt.Printf("==> Binary: %s (%s)\n", bin, key)
	}

	// print sprites
	if len(sprites) > 0 {
		fmt.Printf("Sprites:\n")
		for _, sprite := range sprites {
			fmt.Printf(" - %s\n", sprite)
		}
	}

	// print configs
	for key, cfg := range manifest.Config {
		if len(cfg.Sections) > 0 {
			fmt.Printf("Config (%s):\n", key)
			for _, section := range cfg.Sections {
				fmt.Printf(" - %s\n", section.Title)
				for _, item := range section.Items {
					fmt.Printf("   - %s (%s)\n", item.Key, item.Type)
				}
			}
		}
	}

	/* create bundle */

	// prepare bundle
	var bundle alp.Bundle

	// add attributes
	bundle.AddAttr("name", []byte(manifest.Name))
	bundle.AddAttr("title", []byte(manifest.Title))
	bundle.AddAttr("version", []byte(manifest.Version))

	// add binaries
	for key, bin := range manifest.Binary {
		binPath := filepath.Join(root, bin)
		binData, err := os.ReadFile(binPath)
		if err != nil {
			return err
		}
		bundle.Sections = append(bundle.Sections, alp.BundleSection{
			Type: alp.BundleTypeBinary,
			Name: key,
			Data: binData,
		})
	}

	// add sprites
	for _, sprite := range sprites {
		spriteData, err := os.ReadFile(sprite)
		if err != nil {
			return err
		}
		if filepath.Ext(sprite) == ".png" {
			spriteData = alp.SpriteFromPNG(spriteData, 1).Encode()
			sprite = strings.TrimSuffix(sprite, ".png")
		}
		bundle.Sections = append(bundle.Sections, alp.BundleSection{
			Type: alp.BundleTypeSprite,
			Name: lo.Must(filepath.Rel(root, sprite)),
			Data: spriteData,
		})
	}

	// add configs
	for key, cfg := range manifest.Config {
		if len(cfg.Sections) > 0 {
			configBundle, err := cfg.Encode()
			if err != nil {
				return fmt.Errorf("config %q: %w", key, err)
			}
			bundle.Sections = append(bundle.Sections, alp.BundleSection{
				Type: alp.BundleTypeConfig,
				Name: key,
				Data: configBundle.Encode(),
			})
		}
	}

	// determine output file
	file, err := filepath.Abs(out)
	if err != nil {
		return err
	}

	// create file
	err = os.WriteFile(file, bundle.Encode(), 0644)
	if err != nil {
		return err
	}

	// print bundle info
	fmt.Printf("==> Wrote: %s\n", file)
	if pluginVerbose {
		fmt.Printf("==> Sections: %d\n", len(bundle.Sections))
		for i, section := range bundle.Sections {
			fmt.Printf("    Num=%d Type=%s Name=%q Size=%d\n", i, section.Type, section.Name, len(section.Data))
		}
	}

	return nil
}
