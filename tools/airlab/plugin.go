package main

import "github.com/spf13/cobra"

var pluginVerbose bool

var pluginCmd = &cobra.Command{
	Use:   "plugin",
	Short: "Plugin management tools",
}

func init() {
	pluginCmd.PersistentFlags().BoolVarP(&pluginVerbose, "verbose", "v", false, "Enable verbose output.")

	rootCmd.AddCommand(pluginCmd)
}
