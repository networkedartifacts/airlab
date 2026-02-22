package alp

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
)

type SettingsType string

const (
	SettingsTypeString SettingsType = "string"
	SettingsTypeBool   SettingsType = "bool"
	SettingsTypeInt    SettingsType = "int"
	SettingsTypeFloat  SettingsType = "float"
)

type SettingsOption struct {
	Value any    `yaml:"value"`
	Label string `yaml:"label"`
}

type SettingsItem struct {
	Key     string           `yaml:"key"`
	Title   string           `yaml:"title"`
	Hint    string           `yaml:"hint,omitempty"`
	Type    SettingsType     `yaml:"type"`
	Default any              `yaml:"default"`
	Min     any              `yaml:"min,omitempty"`
	Max     any              `yaml:"max,omitempty"`
	Step    any              `yaml:"step,omitempty"`
	Options []SettingsOption `yaml:"options,omitempty"`
}

type SettingSection struct {
	Title string         `yaml:"title"`
	Hint  string         `yaml:"hint,omitempty"`
	Items []SettingsItem `yaml:"items"`
}

type Settings struct {
	Sections []SettingSection `yaml:"sections"`
}

func (s Settings) Validate() error {
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
			case SettingsTypeString, SettingsTypeBool, SettingsTypeInt, SettingsTypeFloat:
				// ok
			default:
				return fmt.Errorf("item %q: invalid type %q", item.Key, item.Type)
			}

			// check default
			if item.Default != nil {
				if !checkSettingsValue(item.Type, item.Default) {
					return fmt.Errorf("item %q: default value has wrong type", item.Key)
				}
			}

			// check min, max, step
			if item.Min != nil || item.Max != nil || item.Step != nil {
				if item.Type != SettingsTypeInt && item.Type != SettingsTypeFloat {
					return fmt.Errorf("item %q: min/max/step not allowed for type %q", item.Key, item.Type)
				}
				for name, value := range map[string]any{"min": item.Min, "max": item.Max, "step": item.Step} {
					if value != nil && !checkSettingsValue(item.Type, value) {
						return fmt.Errorf("item %q: %s value has wrong type", item.Key, name)
					}
				}
			}

			// check options
			if len(item.Options) > 0 {
				if item.Type != SettingsTypeString && item.Type != SettingsTypeInt {
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
					if !checkSettingsValue(item.Type, opt.Value) {
						return fmt.Errorf("item %q: option %q: wrong value type", item.Key, opt.Label)
					}
				}
			}
		}
	}

	return nil
}

// Coding
//
// Settings are encoded into a Bundle with the following structure:
//
// Section headers are stored as BundleTypeAttr sections named by index
// ("0", "1", ...) with the data formatted as:
//
//   [flags: 1 byte]                — bit0=hint
//   [title: null-terminated string]
//   [hint: null-terminated string]  — if flag set
//
// Items are stored as BundleTypeBinary sections named by key (e.g. "name")
// with the following binary format:
//
//   [section: 1 byte]            — section index
//   [type: 1 byte]               — 0=string, 1=bool, 2=int, 3=float
//   [flags: 1 byte]              — bit0=default, bit1=min, bit2=max,
//                                  bit3=step, bit4=options, bit5=hint
//   [title: null-terminated string]
//   [hint: null-terminated string]  — if flag set
//   [default value]              — if flag set
//   [min value]                  — if flag set
//   [max value]                  — if flag set
//   [step value]                 — if flag set
//   [option count: uint16]       — if flag set
//     [value] [label: null-terminated string] ...
//
// Value encoding by type:
//   string — uint16 length + raw bytes
//   bool   — 1 byte (0 or 1)
//   int    — int32 little-endian
//   float  — float32 little-endian
//

const (
	settingsSectionFlagHint byte = 1 << 0
)

const (
	settingsItemFlagDefault byte = 1 << 0
	settingsItemFlagMin     byte = 1 << 1
	settingsItemFlagMax     byte = 1 << 2
	settingsItemFlagStep    byte = 1 << 3
	settingsItemFlagOptions byte = 1 << 4
	settingsItemFlagHint    byte = 1 << 5
)

func settingsTypeByte(typ SettingsType) byte {
	switch typ {
	case SettingsTypeString:
		return 0
	case SettingsTypeBool:
		return 1
	case SettingsTypeInt:
		return 2
	case SettingsTypeFloat:
		return 3
	default:
		return 0
	}
}

func settingsTypeFromByte(b byte) SettingsType {
	switch b {
	case 0:
		return SettingsTypeString
	case 1:
		return SettingsTypeBool
	case 2:
		return SettingsTypeInt
	case 3:
		return SettingsTypeFloat
	default:
		return ""
	}
}

func encodeSettingsValue(typ SettingsType, value any) []byte {
	switch typ {
	case SettingsTypeString:
		s := value.(string)
		buf := binary.LittleEndian.AppendUint16(nil, uint16(len(s)))
		return append(buf, []byte(s)...)
	case SettingsTypeBool:
		if value.(bool) {
			return []byte{1}
		}
		return []byte{0}
	case SettingsTypeInt:
		return binary.LittleEndian.AppendUint32(nil, uint32(int32(value.(int))))
	case SettingsTypeFloat:
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

func decodeSettingsValue(typ SettingsType, data []byte) (any, int) {
	switch typ {
	case SettingsTypeString:
		if len(data) < 2 {
			return nil, 0
		}
		n := int(binary.LittleEndian.Uint16(data))
		if len(data) < 2+n {
			return nil, 0
		}
		return string(data[2 : 2+n]), 2 + n
	case SettingsTypeBool:
		if len(data) < 1 {
			return nil, 0
		}
		return data[0] != 0, 1
	case SettingsTypeInt:
		if len(data) < 4 {
			return nil, 0
		}
		return int(int32(binary.LittleEndian.Uint32(data))), 4
	case SettingsTypeFloat:
		if len(data) < 4 {
			return nil, 0
		}
		return float64(math.Float32frombits(binary.LittleEndian.Uint32(data))), 4
	default:
		return nil, 0
	}
}

// Encode validates the settings and encodes them into a bundle.
func (s Settings) Encode() (*Bundle, error) {
	// validate first
	err := s.Validate()
	if err != nil {
		return nil, err
	}

	// prepare bundle
	b := &Bundle{}

	// encode sections and items
	for i, section := range s.Sections {
		idx := strconv.Itoa(i)

		// add section header as attr
		var flags byte
		if section.Hint != "" {
			flags |= settingsSectionFlagHint
		}
		var attrData []byte
		attrData = append(attrData, flags)
		attrData = append(attrData, []byte(section.Title)...)
		attrData = append(attrData, 0)
		if section.Hint != "" {
			attrData = append(attrData, []byte(section.Hint)...)
			attrData = append(attrData, 0)
		}
		b.AddAttr(idx, attrData)

		// encode items
		for _, item := range section.Items {
			// determine type byte
			typ := settingsTypeByte(item.Type)

			// determine flags
			var flags byte
			if item.Default != nil {
				flags |= settingsItemFlagDefault
			}
			if item.Min != nil {
				flags |= settingsItemFlagMin
			}
			if item.Max != nil {
				flags |= settingsItemFlagMax
			}
			if item.Step != nil {
				flags |= settingsItemFlagStep
			}
			if len(item.Options) > 0 {
				flags |= settingsItemFlagOptions
			}
			if item.Hint != "" {
				flags |= settingsItemFlagHint
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
				data = append(data, encodeSettingsValue(item.Type, item.Default)...)
			}
			if item.Min != nil {
				data = append(data, encodeSettingsValue(item.Type, item.Min)...)
			}
			if item.Max != nil {
				data = append(data, encodeSettingsValue(item.Type, item.Max)...)
			}
			if item.Step != nil {
				data = append(data, encodeSettingsValue(item.Type, item.Step)...)
			}
			if len(item.Options) > 0 {
				data = binary.LittleEndian.AppendUint16(data, uint16(len(item.Options)))
				for _, opt := range item.Options {
					data = append(data, encodeSettingsValue(item.Type, opt.Value)...)
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

// DecodeSettings parses a bundle back into a Settings struct.
func DecodeSettings(b *Bundle) (*Settings, error) {
	// collect section headers by index
	type sectionHeader struct {
		Title string
		Hint  string
	}
	headers := map[int]sectionHeader{}
	maxIdx := -1
	for _, sec := range b.Sections {
		if sec.Type == BundleTypeAttr {
			idx, err := strconv.Atoi(sec.Name)
			if err != nil {
				return nil, fmt.Errorf("invalid section index %q", sec.Name)
			}

			// parse flags, title and hint
			if len(sec.Data) < 2 {
				return nil, fmt.Errorf("section %d: data too short", idx)
			}
			flags := sec.Data[0]
			rest := sec.Data[1:]
			nullPos := bytes.IndexByte(rest, 0)
			if nullPos < 0 {
				return nil, fmt.Errorf("section %d: missing title null terminator", idx)
			}
			title := string(rest[:nullPos])
			hint := ""
			if flags&settingsSectionFlagHint != 0 {
				rest = rest[nullPos+1:]
				np := bytes.IndexByte(rest, 0)
				if np < 0 {
					return nil, fmt.Errorf("section %d: missing hint null terminator", idx)
				}
				hint = string(rest[:np])
			}

			headers[idx] = sectionHeader{Title: title, Hint: hint}
			if idx > maxIdx {
				maxIdx = idx
			}
		}
	}

	// check for sections
	if maxIdx < 0 {
		return nil, fmt.Errorf("no sections found")
	}

	// build sections
	sections := make([]SettingSection, maxIdx+1)
	for i := 0; i <= maxIdx; i++ {
		hdr, ok := headers[i]
		if !ok {
			return nil, fmt.Errorf("missing section title for index %d", i)
		}
		sections[i].Title = hdr.Title
		sections[i].Hint = hdr.Hint
	}

	// decode items
	for _, sec := range b.Sections {
		if sec.Type != BundleTypeBinary {
			continue
		}

		key := sec.Name
		data := sec.Data
		if len(data) < 4 {
			return nil, fmt.Errorf("item %q: data too short", key)
		}

		// read section index, type and flags
		idx := int(data[0])
		if idx < 0 || idx > maxIdx {
			return nil, fmt.Errorf("item %q: invalid section index %d", key, idx)
		}
		typ := settingsTypeFromByte(data[1])
		if typ == "" {
			return nil, fmt.Errorf("item %q: invalid type byte %d", key, data[1])
		}
		flags := data[2]
		off := 3

		// read title (null-terminated)
		nullPos := -1
		for i := off; i < len(data); i++ {
			if data[i] == 0 {
				nullPos = i
				break
			}
		}
		if nullPos < 0 {
			return nil, fmt.Errorf("item %q: missing title null terminator", key)
		}
		title := string(data[off:nullPos])
		off = nullPos + 1

		// read hint (null-terminated) if flag set
		var hint string
		if flags&settingsItemFlagHint != 0 {
			np := bytes.IndexByte(data[off:], 0)
			if np < 0 {
				return nil, fmt.Errorf("item %q: missing hint null terminator", key)
			}
			hint = string(data[off : off+np])
			off += np + 1
		}

		item := SettingsItem{
			Key:   key,
			Title: title,
			Hint:  hint,
			Type:  typ,
		}

		// decode optional values
		if flags&settingsItemFlagDefault != 0 {
			val, n := decodeSettingsValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode default", key)
			}
			item.Default = val
			off += n
		}
		if flags&settingsItemFlagMin != 0 {
			val, n := decodeSettingsValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode min", key)
			}
			item.Min = val
			off += n
		}
		if flags&settingsItemFlagMax != 0 {
			val, n := decodeSettingsValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode max", key)
			}
			item.Max = val
			off += n
		}
		if flags&settingsItemFlagStep != 0 {
			val, n := decodeSettingsValue(typ, data[off:])
			if n == 0 {
				return nil, fmt.Errorf("item %q: failed to decode step", key)
			}
			item.Step = val
			off += n
		}
		if flags&settingsItemFlagOptions != 0 {
			if len(data[off:]) < 2 {
				return nil, fmt.Errorf("item %q: failed to decode options count", key)
			}
			count := int(binary.LittleEndian.Uint16(data[off:]))
			off += 2
			for i := 0; i < count; i++ {
				val, n := decodeSettingsValue(typ, data[off:])
				if n == 0 {
					return nil, fmt.Errorf("item %q: failed to decode option %d value", key, i)
				}
				off += n
				// read label (null-terminated)
				np := -1
				for j := off; j < len(data); j++ {
					if data[j] == 0 {
						np = j
						break
					}
				}
				if np < 0 {
					return nil, fmt.Errorf("item %q: option %d: missing label null terminator", key, i)
				}
				label := string(data[off:np])
				off = np + 1
				item.Options = append(item.Options, SettingsOption{
					Value: val,
					Label: label,
				})
			}
		}

		sections[idx].Items = append(sections[idx].Items, item)
	}

	return &Settings{Sections: sections}, nil
}

func checkSettingsValue(typ SettingsType, value any) bool {
	switch typ {
	case SettingsTypeString:
		_, ok := value.(string)
		return ok
	case SettingsTypeBool:
		_, ok := value.(bool)
		return ok
	case SettingsTypeInt:
		_, ok := value.(int)
		return ok
	case SettingsTypeFloat:
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
