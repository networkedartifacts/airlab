package alp

import (
	"bytes"
	"image/color"
	"image/png"
)

func convColor(c color.Color) color.RGBA {
	R, G, B, A := c.RGBA()
	return color.RGBA{
		R: uint8(R >> 8),
		G: uint8(G >> 8),
		B: uint8(B >> 8),
		A: uint8(A >> 8),
	}
}

type Sprite struct {
	Width, Height int
	Image, Mask   []byte
}

func SpriteFromPNG(data []byte, scale int) *Sprite {
	// ensure scale
	if scale < 1 {
		scale = 1
	}

	// parse image
	img, err := png.Decode(bytes.NewReader(data))
	if err != nil {
		panic(err)
	}

	// bounds & input size
	b := img.Bounds()
	inW, inH := b.Dx(), b.Dy()

	// output size (scaled)
	outW, outH := inW*scale, inH*scale

	// prepare sprite and mask (1bpp, LSB-first bit order kept)
	image := make([]byte, (outW*outH+7)/8)
	mask := make([]byte, (outW*outH+7)/8)

	setBit := func(buf []byte, pos int) {
		buf[pos>>3] |= 1 << (pos & 7)
	}

	// fill sprite and mask with nearest-neighbor expand
	for y := 0; y < inH; y++ {
		for x := 0; x < inW; x++ {
			c := convColor(img.At(b.Min.X+x, b.Min.Y+y))
			if c.A == 0 {
				continue
			}
			isBlack := (c.R + c.G + c.B) == 0

			// write the scale×scale block
			y0 := y * scale
			x0 := x * scale
			for yy := y0; yy < y0+scale; yy++ {
				rowBase := yy * outW
				for xx := x0; xx < x0+scale; xx++ {
					pos := rowBase + xx
					setBit(mask, pos)
					if isBlack {
						setBit(image, pos)
					}
				}
			}
		}
	}

	return &Sprite{
		Width:  outW,
		Height: outH,
		Image:  image,
		Mask:   mask,
	}
}

func DecodeSprite(als []byte) *Sprite {
	// parse ALS data
	if len(als) < 4 {
		panic("invalid sprite data")
	}
	w := int(als[0]) | int(als[1])<<8
	h := int(als[2]) | int(als[3])<<8
	expectedLen := 4 + (w*h+7)/8*2
	if len(als) < expectedLen {
		panic("invalid sprite data")
	}

	// get image and mask
	image := als[4 : 4+(w*h+7)/8]
	mask := als[4+(w*h+7)/8 : expectedLen]

	return &Sprite{
		Width:  w,
		Height: h,
		Image:  image,
		Mask:   mask,
	}
}

func (s Sprite) Encode() []byte {
	// prepare buffer
	buf := new(bytes.Buffer)

	// add width & height as uint16
	buf.Write(enc.AppendUint16(nil, uint16(s.Width)))
	buf.Write(enc.AppendUint16(nil, uint16(s.Height)))

	// add image & mask
	buf.Write(s.Image)
	buf.Write(s.Mask)

	return buf.Bytes()
}
