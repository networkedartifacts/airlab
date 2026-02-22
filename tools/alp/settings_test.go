package alp

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func validSettings() Settings {
	return Settings{
		Sections: []SettingSection{
			{
				Title: "General",
				Items: []SettingsItem{
					{
						Key:     "name",
						Title:   "Name",
						Type:    SettingsTypeString,
						Default: "default",
					},
				},
			},
		},
	}
}

func TestSettingsValidate(t *testing.T) {
	// valid
	assert.NoError(t, validSettings().Validate())

	// valid with all types
	assert.NoError(t, Settings{
		Sections: []SettingSection{
			{
				Title: "General",
				Items: []SettingsItem{
					{Key: "s", Title: "String", Type: SettingsTypeString, Default: "foo"},
					{Key: "b", Title: "Bool", Type: SettingsTypeBool, Default: true},
					{Key: "i", Title: "Int", Type: SettingsTypeInt, Default: 42, Min: 0, Max: 100},
					{Key: "f", Title: "Float", Type: SettingsTypeFloat, Default: 3.14, Min: 0.0, Max: 10.0, Step: 0.5},
				},
			},
		},
	}.Validate())

	// valid with options
	assert.NoError(t, Settings{
		Sections: []SettingSection{
			{
				Title: "General",
				Items: []SettingsItem{
					{
						Key: "color", Title: "Color", Type: SettingsTypeString,
						Default: "red",
						Options: []SettingsOption{
							{Value: "red", Label: "Red"},
							{Value: "blue", Label: "Blue"},
						},
					},
					{
						Key: "level", Title: "Level", Type: SettingsTypeInt,
						Default: 1,
						Options: []SettingsOption{
							{Value: 1, Label: "Low"},
							{Value: 5, Label: "High"},
						},
					},
				},
			},
		},
	}.Validate())

	// valid with float int coercion
	assert.NoError(t, Settings{
		Sections: []SettingSection{
			{
				Title: "General",
				Items: []SettingsItem{
					{Key: "f", Title: "Float", Type: SettingsTypeFloat, Default: 1, Min: 0, Max: 10},
				},
			},
		},
	}.Validate())
}

func TestSettingsValidateSections(t *testing.T) {
	// missing section title
	s := validSettings()
	s.Sections[0].Title = ""
	assert.EqualError(t, s.Validate(), `section 0: missing title`)

	// empty items
	s = validSettings()
	s.Sections[0].Items = nil
	assert.EqualError(t, s.Validate(), `section "General": no items defined`)
}

func TestSettingsValidateItems(t *testing.T) {
	// missing key
	s := validSettings()
	s.Sections[0].Items[0].Key = ""
	assert.EqualError(t, s.Validate(), `section "General", item 0: missing key`)

	// duplicate key
	s = validSettings()
	s.Sections[0].Items = append(s.Sections[0].Items, s.Sections[0].Items[0])
	assert.EqualError(t, s.Validate(), `section "General", item 1: duplicate key "name"`)

	// missing title
	s = validSettings()
	s.Sections[0].Items[0].Title = ""
	assert.EqualError(t, s.Validate(), `item "name": missing title`)

	// invalid type
	s = validSettings()
	s.Sections[0].Items[0].Type = "unknown"
	assert.EqualError(t, s.Validate(), `item "name": invalid type "unknown"`)

	// empty type
	s = validSettings()
	s.Sections[0].Items[0].Type = ""
	assert.EqualError(t, s.Validate(), `item "name": invalid type ""`)
}

func TestSettingsValidateDefault(t *testing.T) {
	// wrong default for string
	s := validSettings()
	s.Sections[0].Items[0].Default = 42
	assert.EqualError(t, s.Validate(), `item "name": default value has wrong type`)

	// wrong default for bool
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeBool
	s.Sections[0].Items[0].Default = "yes"
	assert.EqualError(t, s.Validate(), `item "name": default value has wrong type`)

	// wrong default for int
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeInt
	s.Sections[0].Items[0].Default = 3.14
	assert.EqualError(t, s.Validate(), `item "name": default value has wrong type`)

	// wrong default for float
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeFloat
	s.Sections[0].Items[0].Default = "nope"
	assert.EqualError(t, s.Validate(), `item "name": default value has wrong type`)

	// nil default is ok
	s = validSettings()
	s.Sections[0].Items[0].Default = nil
	assert.NoError(t, s.Validate())
}

func TestSettingsValidateMinMaxStep(t *testing.T) {
	// not allowed for string
	s := validSettings()
	s.Sections[0].Items[0].Min = "a"
	assert.EqualError(t, s.Validate(), `item "name": min/max/step not allowed for type "string"`)

	// not allowed for bool
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeBool
	s.Sections[0].Items[0].Default = true
	s.Sections[0].Items[0].Max = 1
	assert.EqualError(t, s.Validate(), `item "name": min/max/step not allowed for type "bool"`)

	// wrong type for int min
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeInt
	s.Sections[0].Items[0].Default = 5
	s.Sections[0].Items[0].Min = "zero"
	assert.EqualError(t, s.Validate(), `item "name": min value has wrong type`)

	// wrong type for float max
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeFloat
	s.Sections[0].Items[0].Default = 1.0
	s.Sections[0].Items[0].Max = "ten"
	assert.EqualError(t, s.Validate(), `item "name": max value has wrong type`)
}

func TestSettingsValidateOptions(t *testing.T) {
	// not allowed for bool
	s := validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeBool
	s.Sections[0].Items[0].Default = true
	s.Sections[0].Items[0].Options = []SettingsOption{{Value: true, Label: "Yes"}}
	assert.EqualError(t, s.Validate(), `item "name": options not allowed for type "bool"`)

	// not allowed for float
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeFloat
	s.Sections[0].Items[0].Default = 1.0
	s.Sections[0].Items[0].Options = []SettingsOption{{Value: 1.0, Label: "One"}}
	assert.EqualError(t, s.Validate(), `item "name": options not allowed for type "float"`)

	// not allowed with min/max/step
	s = validSettings()
	s.Sections[0].Items[0].Type = SettingsTypeInt
	s.Sections[0].Items[0].Default = 1
	s.Sections[0].Items[0].Min = 0
	s.Sections[0].Items[0].Options = []SettingsOption{{Value: 1, Label: "One"}}
	assert.EqualError(t, s.Validate(), `item "name": min/max/step not allowed with options`)

	// missing label
	s = validSettings()
	s.Sections[0].Items[0].Options = []SettingsOption{{Value: "a", Label: ""}}
	assert.EqualError(t, s.Validate(), `item "name": option 0: missing label`)

	// missing value
	s = validSettings()
	s.Sections[0].Items[0].Options = []SettingsOption{{Value: nil, Label: "A"}}
	assert.EqualError(t, s.Validate(), `item "name": option "A": missing value`)

	// wrong value type
	s = validSettings()
	s.Sections[0].Items[0].Options = []SettingsOption{{Value: 42, Label: "Bad"}}
	assert.EqualError(t, s.Validate(), `item "name": option "Bad": wrong value type`)
}

func TestSettingsEncodeDecode(t *testing.T) {
	input := Settings{
		Sections: []SettingSection{
			{
				Title: "General",
				Hint:  "Basic configuration options",
				Items: []SettingsItem{
					{
						Key:     "name",
						Title:   "Name",
						Hint:    "The display name",
						Type:    SettingsTypeString,
						Default: "hello",
					},
					{
						Key:     "enabled",
						Title:   "Enabled",
						Type:    SettingsTypeBool,
						Default: true,
					},
					{
						Key:     "count",
						Title:   "Count",
						Hint:    "Number of items",
						Type:    SettingsTypeInt,
						Default: 42,
						Min:     0,
						Max:     100,
						Step:    5,
					},
					{
						Key:     "ratio",
						Title:   "Ratio",
						Type:    SettingsTypeFloat,
						Default: 3.14,
						Min:     0.0,
						Max:     10.0,
						Step:    0.5,
					},
				},
			},
			{
				Title: "Options",
				Items: []SettingsItem{
					{
						Key:     "color",
						Title:   "Color",
						Type:    SettingsTypeString,
						Default: "red",
						Options: []SettingsOption{
							{Value: "red", Label: "Red"},
							{Value: "blue", Label: "Blue"},
						},
					},
					{
						Key:     "level",
						Title:   "Level",
						Type:    SettingsTypeInt,
						Default: 1,
						Options: []SettingsOption{
							{Value: 1, Label: "Low"},
							{Value: 5, Label: "High"},
						},
					},
				},
			},
		},
	}

	// encode
	bundle, err := input.Encode()
	assert.NoError(t, err)
	assert.NotNil(t, bundle)

	// round-trip through bundle binary encoding
	raw := bundle.Encode()
	bundle2, err := DecodeBundle(raw)
	assert.NoError(t, err)

	// decode settings
	output, err := DecodeSettings(bundle2)
	assert.NoError(t, err)

	// compare sections
	assert.Equal(t, len(input.Sections), len(output.Sections))
	for i, sec := range input.Sections {
		assert.Equal(t, sec.Title, output.Sections[i].Title)
		assert.Equal(t, sec.Hint, output.Sections[i].Hint)
		assert.Equal(t, len(sec.Items), len(output.Sections[i].Items))
		for j, item := range sec.Items {
			out := output.Sections[i].Items[j]
			assert.Equal(t, item.Key, out.Key)
			assert.Equal(t, item.Title, out.Title)
			assert.Equal(t, item.Hint, out.Hint)
			assert.Equal(t, item.Type, out.Type)

			// float values decode as float64
			if item.Type == SettingsTypeFloat {
				if item.Default != nil {
					assert.InDelta(t, item.Default, out.Default, 0.001)
				}
				if item.Min != nil {
					assert.InDelta(t, item.Min, out.Min, 0.001)
				}
				if item.Max != nil {
					assert.InDelta(t, item.Max, out.Max, 0.001)
				}
				if item.Step != nil {
					assert.InDelta(t, item.Step, out.Step, 0.001)
				}
			} else {
				assert.Equal(t, item.Default, out.Default)
				assert.Equal(t, item.Min, out.Min)
				assert.Equal(t, item.Max, out.Max)
				assert.Equal(t, item.Step, out.Step)
			}

			assert.Equal(t, len(item.Options), len(out.Options))
			for k, opt := range item.Options {
				assert.Equal(t, opt.Label, out.Options[k].Label)
				assert.Equal(t, opt.Value, out.Options[k].Value)
			}
		}
	}
}
