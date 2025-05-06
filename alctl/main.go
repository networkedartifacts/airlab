package main

import (
	"bytes"
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/gif"
	"image/png"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"golang.org/x/image/bmp"
)

var fast = flag.Bool("fast", false, "fast mode")
var scale = flag.Int("scale", 1, "scale factor")
var format = flag.String("format", "png", "output format")
var grey = flag.Bool("grey", false, "use grey palette")

func makePalette() color.Palette {
	if *grey {
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

func main() {
	// parse flags
	flag.Parse()

	// print flags
	fmt.Println("fast:", *fast)
	fmt.Println("scale:", *scale)
	fmt.Println("format:", *format)

	// handle command
	switch flag.Arg(0) {
	case "convert":
		convert(flag.Arg(1), *format)
	case "animate":
		animate(flag.Arg(1))
	default:
		fmt.Println("unknown command")
		os.Exit(1)
	}
}

func convert(glob string, format string) {
	// get files
	files, err := filepath.Glob(glob)
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	// handle files
	for _, file := range files {
		// log file
		fmt.Println(file)

		// read file
		data, err := os.ReadFile(file)
		if err != nil {
			panic(err)
		}

		// convert image
		img := convertImage(data, makePalette(), *scale)

		// encode image
		var out bytes.Buffer
		if format == "bmp" {
			err = bmp.Encode(&out, img)
		} else {
			err = png.Encode(&out, img)
		}
		if err != nil {
			panic(err)
		}

		// write file
		err = os.WriteFile(strings.ReplaceAll(file, ".bin", "."+format), out.Bytes(), 0644)
		if err != nil {
			panic(err)
		}
	}
}

type file struct {
	name   string
	millis int
	data   []byte
}

func animate(glob string) {
	// get files
	names, err := filepath.Glob(glob)
	if err != nil {
		fmt.Println(err)
		os.Exit(1)
	}

	// load files
	var files []file
	for _, name := range names {
		// parse filename
		var millis int
		_, err = fmt.Sscanf(name, "screen-%d.bin", &millis)
		if err != nil {
			panic(err)
		}

		// read file
		data, err := os.ReadFile(name)
		if err != nil {
			panic(err)
		}

		// append file
		files = append(files, file{
			name:   name,
			millis: millis,
			data:   data,
		})
	}

	// sort files
	sort.Slice(files, func(i, j int) bool {
		return files[i].millis < files[j].millis
	})

	// prepare palette
	palette := makePalette()

	// prepare GIF
	gifImage := &gif.GIF{
		Config: image.Config{
			ColorModel: palette,
			Width:      screenWidth * *scale,
			Height:     screenHeight * *scale,
		},
	}

	// handle files
	for i, file := range files {
		// calculate delay
		delay := 100
		if i < len(files)-1 {
			delay = (files[i+1].millis - file.millis) / 10
			if *fast {
				delay = 5
			}
		}

		// log file
		fmt.Printf("%s (%.2f)\n", file.name, float64(delay)/100)

		// convert image
		img := convertImage(file.data, palette, *scale)

		// add image
		gifImage.Image = append(gifImage.Image, img)
		gifImage.Delay = append(gifImage.Delay, delay)
	}

	// encode GIF
	var out bytes.Buffer
	err = gif.EncodeAll(&out, gifImage)
	if err != nil {
		panic(err)
	}

	// write file
	err = os.WriteFile("animation.gif", out.Bytes(), 0644)
	if err != nil {
		panic(err)
	}
}

const screenWidth = 296
const screenHeight = 128

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
