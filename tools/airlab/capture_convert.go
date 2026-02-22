package main

import (
	"bytes"
	"fmt"
	"image"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
	"golang.org/x/image/bmp"
)

const screenWidth = 296
const screenHeight = 128

var captureFormat string

var captureConvertCmd = &cobra.Command{
	Use:   "convert <glob>",
	Short: "Convert screen capture binary files to images",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		return captureConvert(args[0])
	},
}

func init() {
	captureConvertCmd.Flags().StringVar(&captureFormat, "format", "png", "Set the output image format (png, bmp).")

	captureCmd.AddCommand(captureConvertCmd)
}

func captureConvert(glob string) error {
	// get files
	files, err := filepath.Glob(glob)
	if err != nil {
		return err
	}

	// prepare palette
	palette := makePalette()

	// handle files
	for _, file := range files {
		// log file
		fmt.Println(file)

		// read file
		data, err := os.ReadFile(file)
		if err != nil {
			return err
		}

		// convert image
		img := convertImage(data, palette, captureScale)

		// encode image
		var out bytes.Buffer
		if captureFormat == "bmp" {
			err = bmp.Encode(&out, img)
		} else {
			err = png.Encode(&out, img)
		}
		if err != nil {
			return err
		}

		// write file
		err = os.WriteFile(strings.ReplaceAll(file, ".bin", "."+captureFormat), out.Bytes(), 0644)
		if err != nil {
			return err
		}
	}

	return nil
}

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
