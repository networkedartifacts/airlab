package main

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"

	"github.com/networkedartifacts/airlab/tools/alp"
)

var pluginAnalyzeCmd = &cobra.Command{
	Use:   "analyze <file>",
	Short: "Analyze a plugin bundle file",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		return pluginAnalyze(args[0])
	},
}

func init() {
	pluginCmd.AddCommand(pluginAnalyzeCmd)
}

func pluginAnalyze(file string) error {
	// read file
	data, err := os.ReadFile(file)
	if err != nil {
		return err
	}

	// decode bundle
	bundle, err := alp.DecodeBundle(data)
	if err != nil {
		return err
	}

	// print sections
	fmt.Printf("Sections: (%d total, %d bytes)\n", len(bundle.Sections), len(data))
	for _, sec := range bundle.Sections {
		fmt.Printf(" - [%s] %-20s %d bytes\n", sec.Type, sec.Name, len(sec.Data))
		switch {
		case sec.Type == alp.BundleTypeAttr:
			fmt.Printf("       %s\n", string(sec.Data))
		case sec.Type == alp.BundleTypeConfig:
			sub, err := alp.DecodeBundle(sec.Data)
			if err != nil {
				fmt.Printf("       (failed to decode: %v)\n", err)
				continue
			}
			config, err := alp.DecodeConfig(sub)
			if err != nil {
				fmt.Printf("       (failed to decode: %v)\n", err)
				continue
			}
			for _, section := range config.Sections {
				fmt.Printf("       - %s\n", section.Title)
				for _, item := range section.Items {
					fmt.Printf("         - %s (%s)\n", item.Key, item.Type)
				}
			}
		}
	}

	return nil
}
