package main

import "github.com/spf13/cobra"

var filesExternal bool

var filesCmd = &cobra.Command{
	Use:   "files",
	Short: "Device file management tools",
}

func init() {
	filesCmd.PersistentFlags().BoolVarP(&filesExternal, "external", "e", false, "Use external storage mount.")

	rootCmd.AddCommand(filesCmd)
}

func filesPrefix() string {
	if filesExternal {
		return "/ext"
	}
	return "/int"
}
