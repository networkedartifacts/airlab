package main

import (
	"bytes"
	"image"
	"image/color"
	"image/png"
	"os"
)

const screenWidth = 296
const screenHeight = 128

func makePalette() color.Palette {
	if captureGrey {
		return color.Palette{
			color.RGBA{R: 0xee, G: 0xee, B: 0xee, A: 0xFF},
			color.RGBA{R: 0x22, G: 0x22, B: 0x22, A: 0xFF},
		}
	}
	return color.Palette{
		color.White,
		color.Black,
	}
}

func writePNG(path string, img image.Image) error {
	var buf bytes.Buffer
	err := png.Encode(&buf, img)
	if err != nil {
		return err
	}
	return os.WriteFile(path, buf.Bytes(), 0644)
}

func getBit(data []byte, num int) bool {
	index := num / 8
	offset := uint(num % 8)
	return (data[index]>>(7-offset))&1 == 1
}

func convertImage(data []byte, palette color.Palette, scale int) *image.Paletted {
	// create image
	img := image.NewPaletted(image.Rect(0, 0, screenWidth*scale, screenHeight*scale), palette)

	// generate image
	for y := 0; y < screenHeight; y++ {
		for x := 0; x < screenWidth; x++ {
			xx := screenWidth - x - 1
			if getBit(data, x*screenHeight+y) {
				for i := 0; i < scale; i++ {
					for j := 0; j < scale; j++ {
						img.Set(xx*scale+i, y*scale+j, palette[0])
					}
				}
			} else {
				for i := 0; i < scale; i++ {
					for j := 0; j < scale; j++ {
						img.Set(xx*scale+i, y*scale+j, palette[1])
					}
				}
			}
		}
	}

	return img
}
