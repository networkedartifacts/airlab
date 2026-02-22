package alp

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
)

type ConfigType string

const (
	ConfigTypeString ConfigType = "string"
	ConfigTypeBool   ConfigType = "bool"
	ConfigTypeInt    ConfigType = "int"
	ConfigTypeFloat  ConfigType = "float"
)

type ConfigOption struct {
	Value any    `yaml:"value"`
	Label string `yaml:"label"`
}

type ConfigItem struct {
	Key     string         `yaml:"key"`
	Title   string         `yaml:"title"`
	Hint    string         `yaml:"hint,omitempty"`
	Type    ConfigType     `yaml:"type"`
	Default any            `yaml:"default"`
	Min     any            `yaml:"min,omitempty"`
	Max     any            `yaml:"max,omitempty"`
	Step    any            `yaml:"step,omitempty"`
	Options []ConfigOption `yaml:"options,omitempty"`
}

type ConfigSection struct {
	Title string       `yaml:"title"`
	Hint  string       `yaml:"hint,omitempty"`
	Items []ConfigItem `yaml:"items"`
}

type Config struct {
	Sections []ConfigSection `yaml:"sections"`
}

func (s Config) Validate() error {
	// track keys
	keys := map[string]bool{}

	for i, section := range s.Sections {
		// check title
		if section.Title == "" {
			return fmt.Errorf("section %d: missing title", i)
		}

		// check items
		if len(section.Items) == 0 {
			return fmt.Errorf("section %q: no items defined", section.Title)
		}

		for j, item := range section.Items {
			// check key
			if item.Key == "" {
				return fmt.Errorf("section %q, item %d: missing key", section.Title, j)
			}

			// check duplicate keys
			if keys[item.Key] {
				return fmt.Errorf("section %q, item %d: duplicate key %q", section.Title, j, item.Key)
			}

			// mark key as seen
			keys[item.Key] = true

			// check title
			if item.Title == "" {
				return fmt.Errorf("item %q: missing title", item.Key)
			}

			// check type
			switch item.Type {
			case ConfigTypeString, ConfigTypeBool, ConfigTypeInt, ConfigTypeFloat:
				// ok
			default:
				return fmt.Errorf("item %q: invalid type %q", item.Key, item.Type)
			}

			// check default
			if item.Default != nil {
				if !checkConfigValue(item.Type, item.Default) {
					return fmt.Errorf("item %q: default value has wrong type", item.Key)
				}
			}

			// check min, max, step
			if item.Min != nil || item.Max != nil || item.Step != nil {
				if item.Type != ConfigTypeInt && item.Type != ConfigTypeFloat {
					return fmt.Errorf("item %q: min/max/step not allowed for type %q", item.Key, item.Type)
				}
				for name, value := range map[string]any{"min": item.Min, "max": item.Max, "step": item.Step} {
					if value != nil && !checkConfigValue(item.Type, value) {
						return fmt.Errorf("item %q: %s value has wrong type", item.Key, name)
					}
				}
			}

			// check options
			if len(item.Options) > 0 {
				if item.Type != ConfigTypeString && item.Type != ConfigTypeInt {
					return fmt.Errorf("item %q: options not allowed for type %q", item.Key, item.Type)
				}
				if item.Min != nil || item.Max != nil || item.Step != nil {
					return fmt.Errorf("item %q: min/max/step not allowed with options", item.Key)
				}
				for k, opt := range item.Options {
					if opt.Label == "" {
						return fmt.Errorf("item %q: option %d: missing label", item.Key, k)
					}
					if opt.Value == nil {
						return fmt.Errorf("item %q: option %q: missing value", item.Key, opt.Label)
					}
					if !checkConfigValue(item.Type, opt.Value) {
						return fmt.Errorf("item %q: option %q: wrong value type", item.Key, opt.Label)
					}
				}
			}
		}
	}

	return nil
}

const (
	configTypeSection byte = 0
	configTypeString  byte = 1
	configTypeBool    byte = 2
	configTypeInt     byte = 3
	configTypeFloat   byte = 4
)

const (
	configFlagDefault byte = 1 << 0
	configFlagMin     byte = 1 << 1
	configFlagMax     byte = 1 << 2
	configFlagStep    byte = 1 << 3
	configFlagOptions byte = 1 << 4
	configFlagHint    byte = 1 << 5
)

// ConfigValueTypeByte returns the type byte for a config type.
func ConfigValueTypeByte(typ ConfigType) byte {
	switch typ {
	case ConfigTypeString:
		return configTypeString
	case ConfigTypeBool:
		return configTypeBool
	case ConfigTypeInt:
		return configTypeInt
	case ConfigTypeFloat:
		return configTypeFloat
	default:
		return 0
	}
}

// ConfigTypeFromByte returns the config type for a type byte.
func ConfigTypeFromByte(b byte) ConfigType {
	switch b {
	case configTypeString:
		return ConfigTypeString
	case configTypeBool:
		return ConfigTypeBool
	case configTypeInt:
		return ConfigTypeInt
	case configTypeFloat:
		return ConfigTypeFloat
	default:
		return ""
	}
}

// EncodeConfigValue encodes a typed value into bytes.
func EncodeConfigValue(typ ConfigType, value any) []byte {
	switch typ {
	case ConfigTypeString:
		s := value.(string)
		return append([]byte(s), 0)
	case ConfigTypeBool:
		if value.(bool) {
			return []byte{1}
		}
		return []byte{0}
	case ConfigTypeInt:
		return binary.LittleEndian.AppendUint32(nil, uint32(int32(value.(int))))
	case ConfigTypeFloat:
		var f float32
		switch v := value.(type) {
		case float64:
			f = float32(v)
		case int:
			f = float32(v)
		}
		return binary.LittleEndian.AppendUint32(nil, math.Float32bits(f))
	default:
		return nil
	}
}

// DecodeConfigValue decodes a typed value from bytes.
func DecodeConfigValue(typ ConfigType, data []byte) (any, int) {
	switch typ {
	case ConfigTypeString:
		n := bytes.IndexByte(data, 0)
		if n < 0 {
			return nil, 0
		}
		return string(data[:n]), n + 1
	case ConfigTypeBool:
		if len(data) < 1 {
			return nil, 0
		}
		return data[0] != 0, 1
	case ConfigTypeInt:
		if len(data) < 4 {
			return nil, 0
		}
		return int(int32(binary.LittleEndian.Uint32(data))), 4
	case ConfigTypeFloat:
		if len(data) < 4 {
			return nil, 0
		}
		return float64(math.Float32frombits(binary.LittleEndian.Uint32(data))), 4
	default:
		return nil, 0
	}
}

// Encode validates the config and encodes it into a bundle.
func (s Config) Encode() (*Bundle, error) {
	// validate first
	err := s.Validate()
	if err != nil {
		return nil, err
	}

	// prepare bundle
	b := &Bundle{}

	// encode sections and items
	for i, section := range s.Sections {
		// determine section flags
		var sflags byte
		if section.Hint != "" {
			sflags |= configFlagHint
		}

		// build section data
		var sdata []byte
		sdata = append(sdata, byte(i), configTypeSection, sflags)
		sdata = append(sdata, []byte(section.Title)...)
		sdata = append(sdata, 0)
		if section.Hint != "" {
			sdata = append(sdata, []byte(section.Hint)...)
			sdata = append(sdata, 0)
		}

		// add section as binary
		b.Sections = append(b.Sections, BundleSection{
			Type: BundleTypeBinary,
			Name: strconv.Itoa(i),
			Data: sdata,
		})

		// encode items
		for _, item := range section.Items {
			// determine type byte
			typ := ConfigValueTypeByte(item.Type)

			// determine flags
			var flags byte
			if item.Default != nil {
				flags |= configFlagDefault
			}
			if item.Min != nil {
				flags |= configFlagMin
			}
			if item.Max != nil {
				flags |= configFlagMax
			}
			if item.Step != nil {
				flags |= configFlagStep
			}
			if len(item.Options) > 0 {
				flags |= configFlagOptions
			}
			if item.Hint != "" {
				flags |= configFlagHint
			}

			// build data
			var data []byte
			data = append(data, byte(i), typ, flags)

			// write title as null-terminated string
			data = append(data, []byte(item.Title)...)
			data = append(data, 0)

			// write hint as null-terminated string
			if item.Hint != "" {
				data = append(data, []byte(item.Hint)...)
				data = append(data, 0)
			}

			// write optional values
			if item.Default != nil {
				data = append(data, EncodeConfigValue(item.Type, item.Default)...)
			}
			if item.Min != nil {
				data = append(data, EncodeConfigValue(item.Type, item.Min)...)
			}
			if item.Max != nil {
				data = append(data, EncodeConfigValue(item.Type, item.Max)...)
			}
			if item.Step != nil {
				data = append(data, EncodeConfigValue(item.Type, item.Step)...)
			}
			if len(item.Options) > 0 {
				data = binary.LittleEndian.AppendUint16(data, uint16(len(item.Options)))
				for _, opt := range item.Options {
					data = append(data, EncodeConfigValue(item.Type, opt.Value)...)
					data = append(data, []byte(opt.Label)...)
					data = append(data, 0)
				}
			}

			// add as binary section
			b.Sections = append(b.Sections, BundleSection{
				Type: BundleTypeBinary,
				Name: item.Key,
				Data: data,
			})
		}
	}

	return b, nil
}

// DecodeConfig parses a bundle back into a Config struct.
func DecodeConfig(b *Bundle) (*Config, error) {
	// collect section headers and items
	type sectionHeader struct {
		Title string
		Hint  string
	}
	headers := map[int]sectionHeader{}
	maxIdx := -1

	// first pass: collect sections and find max index
	for _, sec := range b.Sections {
		if sec.Type != BundleTypeBinary || len(sec.Data) < 3 {
			continue
		}
		if sec.Data[1] != configTypeSection {
			continue
		}

		idx := int(sec.Data[0])
		flags := sec.Data[2]
		off := 3

		// read title (null-terminated)
		np := bytes.IndexByte(sec.Data[off:], 0)
		if np < 0 {
			return nil, fmt.Errorf("section %d: missing title null terminator", idx)
		}
		title := string(sec.Data[off : off+np])
		off += np + 1

		// read hint (null-terminated) if flag set
		hint := ""
		if flags&configFlagHint != 0 {
			np = bytes.IndexByte(sec.Data[off:], 0)
			if np < 0 {
				return nil, fmt.Errorf("section %d: missing hint null terminator", idx)
			}
			hint = string(sec.Data[off : off+np])
		}

		headers[idx] = sectionHeader{Title: title, Hint: hint}
		if idx > maxIdx {
			maxIdx = idx
		}
	}

	// check for sections
	if maxIdx < 0 {
		return nil, fmt.Errorf("no sections found")
	}

	// build sections
	sections := make([]ConfigSection, maxIdx+1)
	for i := 0; i <= maxIdx; i++ {
		hdr, ok := headers[i]
		if !ok {
			return nil, fmt.Errorf("missing section title for index %d", i)
		}
		sections[i].Title = hdr.Title
		sections[i].Hint = hdr.Hint
	}

	// second pass: decode items
	for _, sec := range b.Sections {
		if sec.Type != BundleTypeBinary || len(sec.Data) < 4 {
			continue
		}
		if sec.Data[1] == configTypeSection {
			continue
		}

		key := sec.Name
		data := sec.Data

		// read section index, type and flags
		idx := int(data[0])
		if idx > maxIdx {
			return nil, fmt.Errorf("item %q: invalid section index %d", key, idx)
		}
		typ := ConfigTypeFromByte(data[1])
		if typ == "" {
			return nil, fmt.Errorf("item %q: invalid type byte %d", key, data[1])
		}
		flags := data[2]
		off := 3

		// read title (null-terminated)
		nullPos := bytes.IndexByte(data[off:], 0)
		if nullPos < 0 {
			return nil, fmt.Errorf("item %q: missing title null terminator", key)
		}
		title := string(data[off : off+nullPos])
		off += nullPos + 1

		// read hint (null-terminated) if flag set
		var hint string
		if flags&configFlagHint != 0 {
			np := bytes.IndexByte(data[off:], 0)
			if np < 0 {
				return nil, fmt.Errorf("item %q: missing hint null terminator", key)
			}
			hint = string(data[off : off+np])
			off += np + 1
		}

		item := ConfigItem{
			Key:   key,
			Title: title,
			Hint:  hint,
			Type:  typ,
		}

		// decode optional values
		if flags&configFlagDefault != 0 {
			val, n := DecodeConfigValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode default", key)
			}
			item.Default = val
			off += n
		}
		if flags&configFlagMin != 0 {
			val, n := DecodeConfigValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode min", key)
			}
			item.Min = val
			off += n
		}
		if flags&configFlagMax != 0 {
			val, n := DecodeConfigValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode max", key)
			}
			item.Max = val
			off += n
		}
		if flags&configFlagStep != 0 {
			val, n := DecodeConfigValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode step", key)
			}
			item.Step = val
			off += n
		}
		if flags&configFlagOptions != 0 {
			if len(data[off:]) < 2 {
				return nil, fmt.Errorf("item %q: failed to decode options count", key)
			}
			count := int(binary.LittleEndian.Uint16(data[off:]))
			off += 2
			for i := 0; i < count; i++ {
				val, n := DecodeConfigValue(typ, data[off:])
				if n == 0 {
					return nil, fmt.Errorf("item %q: failed to decode option %d value", key, i)
				}
				off += n
				// read label (null-terminated)
				np := bytes.IndexByte(data[off:], 0)
				if np < 0 {
					return nil, fmt.Errorf("item %q: option %d: missing label null terminator", key, i)
				}
				label := string(data[off : off+np])
				off = off + np + 1
				item.Options = append(item.Options, ConfigOption{
					Value: val,
					Label: label,
				})
			}
		}

		sections[idx].Items = append(sections[idx].Items, item)
	}

	return &Config{Sections: sections}, nil
}

func checkConfigValue(typ ConfigType, value any) bool {
	switch typ {
	case ConfigTypeString:
		_, ok := value.(string)
		return ok
	case ConfigTypeBool:
		_, ok := value.(bool)
		return ok
	case ConfigTypeInt:
		_, ok := value.(int)
		return ok
	case ConfigTypeFloat:
		switch value.(type) {
		case float64, int:
			return true
		default:
			return false
		}
	default:
		return false
	}
}
