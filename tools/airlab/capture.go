package main

import "github.com/spf13/cobra"

var captureScale int
var captureGrey bool

var captureCmd = &cobra.Command{
	Use:   "capture",
	Short: "Screen capture image tools",
}

func init() {
	captureCmd.PersistentFlags().IntVar(&captureScale, "scale", 1, "Set the pixel scale factor.")
	captureCmd.PersistentFlags().BoolVar(&captureGrey, "grey", false, "Use a grey color palette.")

	rootCmd.AddCommand(captureCmd)
}
