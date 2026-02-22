package main

import (
	"bytes"
	"fmt"
	"image"
	"image/gif"
	"os"
	"path/filepath"
	"sort"

	"github.com/spf13/cobra"
)

var captureAnimateFast bool

var captureAnimateCmd = &cobra.Command{
	Use:   "animate <glob>",
	Short: "Create GIF animation from screen capture binary files",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		return captureAnimate(args[0])
	},
}

func init() {
	captureAnimateCmd.Flags().BoolVar(&captureAnimateFast, "fast", false, "Use a fixed frame delay for faster playback.")

	captureCmd.AddCommand(captureAnimateCmd)
}

type captureFile struct {
	name   string
	millis int
	data   []byte
}

func captureAnimate(glob string) error {
	// get files
	names, err := filepath.Glob(glob)
	if err != nil {
		return err
	}

	// load files
	var files []captureFile
	for _, name := range names {
		// parse filename
		var millis int
		_, err = fmt.Sscanf(name, "screen-%d.bin", &millis)
		if err != nil {
			return err
		}

		// read file
		data, err := os.ReadFile(name)
		if err != nil {
			return err
		}

		// append file
		files = append(files, captureFile{
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
			Width:      screenWidth * captureScale,
			Height:     screenHeight * captureScale,
		},
	}

	// handle files
	for i, file := range files {
		// calculate delay
		delay := 100
		if i < len(files)-1 {
			delay = (files[i+1].millis - file.millis) / 10
			if captureAnimateFast {
				delay = 5
			}
		}

		// log file
		fmt.Printf("%s (%.2f)\n", file.name, float64(delay)/100)

		// convert image
		img := convertImage(file.data, palette, captureScale)

		// add image
		gifImage.Image = append(gifImage.Image, img)
		gifImage.Delay = append(gifImage.Delay, delay)
	}

	// encode GIF
	var out bytes.Buffer
	err = gif.EncodeAll(&out, gifImage)
	if err != nil {
		return err
	}

	// write file
	err = os.WriteFile("animation.gif", out.Bytes(), 0644)
	if err != nil {
		return err
	}

	return nil
}
