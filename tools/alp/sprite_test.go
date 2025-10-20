package alp

import (
	"bytes"
	"image"
	"image/color"
	"image/png"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestSprite(t *testing.T) {
	s1 := &Sprite{
		Width:  4,
		Height: 4,
		Image:  []byte{0b11110000, 0b00001111},
		Mask:   []byte{0b11111111, 0b11111111},
	}

	buf := s1.Encode()
	assert.Equal(t, []byte{
		0x4, 0x0, // width
		0x4, 0x0, // height
		0b11110000, 0b00001111, // image
		0b11111111, 0b11111111, // mask
	}, buf)

	s2 := DecodeSprite(buf)
	assert.Equal(t, s1, s2)
}

func TestSpriteFromPNG(t *testing.T) {
	img := image.NewRGBA(image.Rect(0, 0, 4, 4))
	for y := 0; y < 4; y++ {
		for x := 0; x < 4; x++ {
			if y < 2 {
				img.SetRGBA(x, y, color.RGBA{R: 0, G: 0, B: 0, A: 255})
			} else {
				img.SetRGBA(x, y, color.RGBA{R: 255, G: 255, B: 255, A: 255})
			}
		}
	}

	var buf = bytes.Buffer{}
	err := png.Encode(&buf, img)
	assert.NoError(t, err)

	sprite := SpriteFromPNG(buf.Bytes(), 1)
	assert.Equal(t, 4, sprite.Width)
	assert.Equal(t, 4, sprite.Height)
	expectedImage := []byte{
		0b11111111, 0b00000000,
	}
	expectedMask := []byte{
		0b11111111, 0b11111111,
	}
	assert.Equal(t, expectedImage, sprite.Image)
	assert.Equal(t, expectedMask, sprite.Mask)
}
