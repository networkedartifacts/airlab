package main

import (
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/256dpi/naos/pkg/msg"
	"github.com/256dpi/naos/pkg/serial"
	"github.com/spf13/cobra"
)

var pluginUploadCmd = &cobra.Command{
	Use:   "upload <input> [device]",
	Short: "Upload a plugin bundle to a device",
	Args:  cobra.RangeArgs(1, 2),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 1 {
			device = args[1]
		}
		return pluginUpload(args[0], device)
	},
}

func init() {
	pluginCmd.AddCommand(pluginUploadCmd)
}

func pluginUpload(input, device string) error {
	// read file
	data, err := os.ReadFile(input)
	if err != nil {
		return err
	}

	// get file name
	name := filepath.Base(input)

	// log
	fmt.Printf("==> Read: %s (%d)\n", name, len(data))

	// open device
	var dev msg.Device
	if device != "" {
		dev, err = serial.Open(device)
	} else {
		dev, err = serial.OpenBest()
	}
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Found: %s\n", dev.ID())

	// prepare device
	man := msg.NewManagedDevice(dev)
	defer man.Deactivate()
	err = man.Activate()
	if err != nil {
		return err
	}

	// get parameters
	var appName []byte
	var appVersion []byte
	var deviceName []byte
	err = man.UseSession(func(s *msg.Session) error {
		appName, err = msg.GetParam(s, "app-name", time.Second)
		if err != nil {
			return err
		}
		appVersion, err = msg.GetParam(s, "app-version", time.Second)
		if err != nil {
			return err
		}
		deviceName, err = msg.GetParam(s, "device-name", time.Second)
		if err != nil {
			return err
		}
		return nil
	})
	if err != nil {
		return err
	}

	// print info
	fmt.Printf("  App Name: %s\n", string(appName))
	fmt.Printf("  App Version: %s\n", string(appVersion))
	fmt.Printf("  Device Name: %s\n", string(deviceName))

	// upload file
	fmt.Printf("==> Uploading: 0%%\n")
	err = man.UseSession(func(s *msg.Session) error {
		err = msg.MakePath(s, "/int/engine", time.Second)
		if err != nil {
			return err
		}
		return msg.WriteFile(s, "/int/engine/"+name, data, func(u uint32) {
			fmt.Printf("\033[A\033[2K\r")
			fmt.Printf("==> Uploading: %.0f%%\n", float64(u)/float64(len(data))*100)
		}, time.Minute)
	})
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Done!\n")

	return nil
}
