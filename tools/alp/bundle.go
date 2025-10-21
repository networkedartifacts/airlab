package alp

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"hash/crc32"
)

var enc = binary.LittleEndian

type BundleType byte

const (
	BundleTypeAttr   BundleType = 0
	BundleTypeBinary BundleType = 1
	BundleTypeSprite BundleType = 2
)

func (bt BundleType) String() string {
	switch bt {
	case BundleTypeAttr:
		return "A"
	case BundleTypeBinary:
		return "B"
	case BundleTypeSprite:
		return "S"
	default:
		return "?"
	}
}

type BundleSection struct {
	Type BundleType
	Name string
	Data []byte
}

type Bundle struct {
	Sections []BundleSection
}

func DecodeBundle(data []byte) (*Bundle, error) {
	// check length
	if len(data) < 10 {
		return nil, fmt.Errorf("invalid bundle: too short")
	}

	// check magic
	if string(data[0:4]) != "ALP\x00" {
		return nil, fmt.Errorf("invalid bundle: bad magic")
	}

	// check header length
	headerLen := int(enc.Uint32(data[4:8]))
	if headerLen > len(data) || headerLen < 10 {
		return nil, fmt.Errorf("invalid bundle: bad header length")
	}

	// read sections
	sections := int(enc.Uint16(data[8:10]))

	// prepare bundle
	b := &Bundle{}

	// read section headers
	offset := 10
	var offsets []int
	var checksums []uint32
	for i := 0; i < sections; i++ {
		// check length
		if offset+9 > len(data) {
			return nil, fmt.Errorf("invalid bundle: section %d: header too short", i)
		}

		// read section type
		typ := BundleType(data[offset])
		offset += 1

		// read section offset
		offsets = append(offsets, int(enc.Uint32(data[offset:offset+4])))
		offset += 4

		// read section size
		size := enc.Uint32(data[offset : offset+4])
		offset += 4

		// read section checksum
		checksums = append(checksums, enc.Uint32(data[offset:offset+4]))
		offset += 4

		// read section name
		nameLen := bytes.IndexByte(data[offset:], 0)
		if nameLen < 0 {
			return nil, fmt.Errorf("invalid bundle: section %d: missing null terminator", i)
		}
		name := string(data[offset : offset+nameLen])
		offset += nameLen + 1

		// add section
		b.Sections = append(b.Sections, BundleSection{
			Type: typ,
			Name: name,
			Data: make([]byte, size),
		})
	}

	// check lengths
	if offset != headerLen {
		return nil, fmt.Errorf("invalid bundle: header length mismatch")
	}

	// read section data
	for i, s := range b.Sections {
		if offsets[i]+len(s.Data) > len(data) {
			return nil, fmt.Errorf("invalid bundle: section %d: missing data", i)
		} else if offset != offsets[i] {
			return nil, fmt.Errorf("invalid bundle: section %d: offset mismatch", i)
		}
		b.Sections[i].Data = data[offsets[i] : offsets[i]+len(s.Data)]
		offset += len(s.Data) + 1
		checksum := crc32.NewIEEE()
		_, _ = checksum.Write(b.Sections[i].Data)
		if checksum.Sum32() != checksums[i] {
			return nil, fmt.Errorf("invalid bundle: section %d: checksum mismatch", i)
		}
	}

	return b, nil
}

func (b *Bundle) AddAttr(name string, value []byte) {
	b.Sections = append(b.Sections, BundleSection{
		Type: BundleTypeAttr,
		Name: name,
		Data: value,
	})
}

func (b *Bundle) GetAttr(name string) []byte {
	for _, section := range b.Sections {
		if section.Type == BundleTypeAttr && section.Name == name {
			return section.Data
		}
	}
	return nil
}

func (b *Bundle) Encode() []byte {
	// prepare buffer
	var out []byte

	// calculate header size
	headerLength := 10
	for _, section := range b.Sections {
		headerLength += 1 + 4 + 4 + 4 + len(section.Name) + 1
	}

	// write header
	out = append(out, 'A', 'L', 'P', 0x00)
	out = enc.AppendUint32(out, uint32(headerLength))
	out = enc.AppendUint16(out, uint16(len(b.Sections)))

	// write section headers
	offset := headerLength
	for _, section := range b.Sections {
		checksum := crc32.NewIEEE()
		_, _ = checksum.Write(section.Data)
		out = append(out, byte(section.Type))
		out = enc.AppendUint32(out, uint32(offset))
		out = enc.AppendUint32(out, uint32(len(section.Data)))
		out = enc.AppendUint32(out, checksum.Sum32())
		out = append(out, []byte(section.Name)...)
		out = append(out, 0)
		offset += len(section.Data) + 1
	}

	// write section data
	for _, section := range b.Sections {
		out = append(out, section.Data...)
		out = append(out, 0)
	}

	return out
}
