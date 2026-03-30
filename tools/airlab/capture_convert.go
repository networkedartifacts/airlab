package main

import (
	"bytes"
	"fmt"
	"image/png"
	"os"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
	"golang.org/x/image/bmp"
)

var captureConvertFormat string

var captureConvertCmd = &cobra.Command{
	Use:   "convert <glob>",
	Short: "Convert screen capture binary files to images",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		return captureConvert(args[0])
	},
}

func init() {
	captureConvertCmd.Flags().StringVar(&captureConvertFormat, "format", "png", "Set the output image format (png, bmp).")

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
		if captureConvertFormat == "bmp" {
			err = bmp.Encode(&out, img)
		} else {
			err = png.Encode(&out, img)
		}
		if err != nil {
			return err
		}

		// write file
		err = os.WriteFile(strings.ReplaceAll(file, ".bin", "."+captureConvertFormat), out.Bytes(), 0644)
		if err != nil {
			return err
		}
	}

	return nil
}

