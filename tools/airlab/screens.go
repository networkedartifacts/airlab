package main

import "github.com/spf13/cobra"

var screensDevice string

var screensCmd = &cobra.Command{
	Use:   "screens",
	Short: "Idle screen management tools",
}

func init() {
	screensCmd.PersistentFlags().StringVarP(&screensDevice, "device", "d", "", "Serial device path.")

	rootCmd.AddCommand(screensCmd)
}
