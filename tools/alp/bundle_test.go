package alp

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestBundle(t *testing.T) {
	b := Bundle{
		Sections: []BundleSection{
			{
				Type: BundleTypeAttr,
				Name: "name",
				Data: []byte("TestApp"),
			},
			{
				Type: BundleTypeBinary,
				Name: "main",
				Data: []byte{0x1, 0x2, 0x3, 0x4},
			},
		},
	}

	out := b.Encode()
	assert.Equal(t, []byte{
		'A', 'L', 'P', 0x00, // magic + version
		0x2e, 0x0, 0x0, 0x0, // header length
		0x2, 0x0, // section count
		// section 1
		0x0,                 // type
		0x2e, 0x0, 0x0, 0x0, // offset
		0x7, 0x0, 0x0, 0x0, // length
		0xda, 0xce, 0x82, 0x46, // checksum
		'n', 'a', 'm', 'e', 0x0, // name + null
		// section 2
		0x1,                 // type
		0x36, 0x0, 0x0, 0x0, // offset
		0x4, 0x0, 0x0, 0x0, // length
		0xcd, 0xfb, 0x3c, 0xb6, // checksum
		'm', 'a', 'i', 'n', 0x0, // name + null
		// section Data
		'T', 'e', 's', 't', 'A', 'p', 'p', 0x0, // section 1 + null
		0x1, 0x2, 0x3, 0x4, 0x0, // section 2 + null
	}, out)

	b2, err := DecodeBundle(out)
	assert.NoError(t, err)
	assert.Equal(t, b, *b2)
}
