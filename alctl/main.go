package main

import (
	"bytes"
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/gif"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"golang.org/x/image/bmp"
)

var fast = flag.Bool("fast", false, "fast mode")

func main() {
	// parse flags
	flag.Parse()

	// handle command
	switch flag.Arg(0) {
	case "convert":
		convert(flag.Arg(1))
	case "animate":
		animate(flag.Arg(1))
	default:
		fmt.Println("unknown command")
		os.Exit(1)
	}
}

func convert(glob string) {
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
		img := convertImage(data, color.Palette{
			color.White,
			color.Black,
		})

		// encode image
		var out bytes.Buffer
		err = bmp.Encode(&out, img)
		if err != nil {
			panic(err)
		}

		// write file
		err = os.WriteFile(strings.ReplaceAll(file, ".bin", ".bmp"), out.Bytes(), 0644)
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
	palette := color.Palette{
		color.White,
		color.Black,
	}

	// prepare GIF
	gifImage := &gif.GIF{
		Config: image.Config{
			ColorModel: palette,
			Width:      screenWidth,
			Height:     screenHeight,
		},
	}

	// prepare delay
	var lastMillis int

	// handle files
	for i, file := range files {
		// log file
		fmt.Println(file.name)

		// convert image
		img := convertImage(file.data, palette)

		// calculate delay
		delay := 0
		if i > 0 {
			delay = (file.millis - lastMillis) / 10
			if *fast {
				delay = 5
			}
		}

		// add image
		gifImage.Image = append(gifImage.Image, img)
		gifImage.Delay = append(gifImage.Delay, delay)

		// update last millis
		lastMillis = file.millis
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

func convertImage(data []byte, palette color.Palette) *image.Paletted {
	// create image
	img := image.NewPaletted(image.Rect(0, 0, screenWidth, screenHeight), palette)

	// generate image
	for y := 0; y < screenHeight; y++ {
		for x := 0; x < screenWidth; x++ {
			if getBit(data, x*screenHeight+y) {
				img.Set(screenWidth-x-1, y, color.RGBA{R: 255, G: 255, B: 255, A: 255})
			} else {
				img.Set(screenWidth-x-1, y, color.RGBA{A: 255})
			}
		}
	}

	return img
}
