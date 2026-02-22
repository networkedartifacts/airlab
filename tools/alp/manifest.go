package alp

import (
	"fmt"
	"os"
	"path/filepath"

	"golang.org/x/mod/semver"
	"gopkg.in/yaml.v3"
)

// Manifest describes a plugin manifest (alp.yml).
type Manifest struct {
	Name    string            `yaml:"name"`
	Title   string            `yaml:"title"`
	Version string            `yaml:"version"`
	Binary  map[string]string `yaml:"binary"`
	Config  map[string]Config `yaml:"config"`
	Sprites []string          `yaml:"sprites"`
}

// LoadManifest loads and parses a manifest from the given directory.
func LoadManifest(dir string) (*Manifest, error) {
	// determine root
	root, err := filepath.Abs(dir)
	if err != nil {
		return nil, err
	}

	// read file
	data, err := os.ReadFile(filepath.Join(root, "alp.yml"))
	if err != nil {
		return nil, err
	}

	// parse
	var m Manifest
	err = yaml.Unmarshal(data, &m)
	if err != nil {
		return nil, err
	}

	// validate
	err = m.Validate()
	if err != nil {
		return nil, err
	}

	return &m, nil
}

// Validate checks the manifest for required fields and constraints.
func (m *Manifest) Validate() error {
	// check required fields
	if m.Name == "" {
		return fmt.Errorf("missing name")
	}
	if m.Title == "" {
		return fmt.Errorf("missing title")
	}
	if m.Version == "" {
		return fmt.Errorf("missing version")
	}
	if !semver.IsValid(m.Version) {
		return fmt.Errorf("invalid version")
	}

	// validate binary keys
	for key := range m.Binary {
		if key != "main" {
			return fmt.Errorf("unsupported binary key: %q (only \"main\" is supported)", key)
		}
	}

	// validate config keys
	for key := range m.Config {
		if key != "main" {
			return fmt.Errorf("unsupported config key: %q (only \"main\" is supported)", key)
		}
	}

	// validate configs
	for key, cfg := range m.Config {
		if err := cfg.Validate(); err != nil {
			return fmt.Errorf("config %q: %w", key, err)
		}
	}

	return nil
}
