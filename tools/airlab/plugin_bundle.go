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
	Name    string   `yaml:"name"`
	Title   string   `yaml:"title"`
	Version string   `yaml:"version"`
	Binary  string   `yaml:"binary"`
	Sprites []string `yaml:"sprites"`
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
	fmt.Printf("==> Binary: %s\n", manifest.Binary)

	// print sprites
	if len(sprites) > 0 {
		fmt.Printf("Sprites:\n")
		for _, sprite := range sprites {
			fmt.Printf(" - %s\n", sprite)
		}
	}

	/* create bundle */

	// prepare bundle
	var bundle alp.Bundle

	// add attributes
	bundle.AddAttr("name", []byte(manifest.Name))
	bundle.AddAttr("title", []byte(manifest.Title))
	bundle.AddAttr("version", []byte(manifest.Version))

	// add binary
	binPath := filepath.Join(root, manifest.Binary)
	binData, err := os.ReadFile(binPath)
	if err != nil {
		return err
	}
	bundle.Sections = append(bundle.Sections, alp.BundleSection{
		Type: alp.BundleTypeBinary,
		Name: "main",
		Data: binData,
	})

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
