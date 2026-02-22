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

var filesUploadCmd = &cobra.Command{
	Use:   "upload <local-file> <remote-path> [device]",
	Short: "Upload a file to a device",
	Args:  cobra.RangeArgs(2, 3),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 2 {
			device = args[2]
		}
		return filesUpload(args[0], args[1], device)
	},
}

var filesDownloadCmd = &cobra.Command{
	Use:   "download <remote-path> <local-file> [device]",
	Short: "Download a file from a device",
	Args:  cobra.RangeArgs(2, 3),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 2 {
			device = args[2]
		}
		return filesDownload(args[0], args[1], device)
	},
}

var filesListCmd = &cobra.Command{
	Use:   "list <remote-path> [device]",
	Short: "List files on a device",
	Args:  cobra.RangeArgs(1, 2),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 1 {
			device = args[1]
		}
		return filesList(args[0], device)
	},
}

var filesRemoveCmd = &cobra.Command{
	Use:   "remove <remote-path> [device]",
	Short: "Remove a file or directory from a device",
	Args:  cobra.RangeArgs(1, 2),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 1 {
			device = args[1]
		}
		return filesRemove(args[0], device)
	},
}

func init() {
	filesCmd.AddCommand(filesUploadCmd)
	filesCmd.AddCommand(filesDownloadCmd)
	filesCmd.AddCommand(filesListCmd)
	filesCmd.AddCommand(filesRemoveCmd)
}

func filesOpenDevice(device string) (*msg.ManagedDevice, error) {
	// open device
	var dev msg.Device
	var err error
	if device != "" {
		dev, err = serial.Open(device)
	} else {
		dev, err = serial.OpenBest()
	}
	if err != nil {
		return nil, err
	}

	// log
	fmt.Printf("==> Found: %s\n", dev.ID())

	// prepare managed device
	man := msg.NewManagedDevice(dev)
	err = man.Activate()
	if err != nil {
		return nil, err
	}

	return man, nil
}

func filesUpload(localFile, remotePath, device string) error {
	// prepend storage prefix
	remotePath = filesPrefix() + remotePath

	// read file
	data, err := os.ReadFile(localFile)
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Read: %s (%d bytes)\n", filepath.Base(localFile), len(data))

	// open device
	man, err := filesOpenDevice(device)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// upload file
	fmt.Printf("==> Uploading: 0%%\n")
	err = man.UseSession(func(s *msg.Session) error {
		// ensure parent directory
		dir := filepath.Dir(remotePath)
		if dir != "" && dir != "/" {
			err := msg.MakePath(s, dir, time.Second)
			if err != nil {
				return err
			}
		}
		return msg.WriteFile(s, remotePath, data, func(u uint32) {
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

func filesDownload(remotePath, localFile, device string) error {
	// prepend storage prefix
	remotePath = filesPrefix() + remotePath

	// open device
	man, err := filesOpenDevice(device)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// download file
	var data []byte
	fmt.Printf("==> Downloading: 0%%\n")
	err = man.UseSession(func(s *msg.Session) error {
		var err error
		data, err = msg.ReadFile(s, remotePath, func(u uint32) {
			fmt.Printf("\033[A\033[2K\r")
			fmt.Printf("==> Downloading: %d bytes\n", u)
		}, time.Minute)
		return err
	})
	if err != nil {
		return err
	}

	// write file
	err = os.WriteFile(localFile, data, 0644)
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Wrote: %s (%d bytes)\n", localFile, len(data))

	return nil
}

func filesList(remotePath, device string) error {
	// prepend storage prefix
	remotePath = filesPrefix() + remotePath

	// open device
	man, err := filesOpenDevice(device)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// list directory
	var entries []msg.FSInfo
	err = man.UseSession(func(s *msg.Session) error {
		var err error
		entries, err = msg.ListDir(s, remotePath, time.Second*5)
		return err
	})
	if err != nil {
		return err
	}

	// print entries
	for _, e := range entries {
		if e.IsDir {
			fmt.Printf("  [dir]  %s\n", e.Name)
		} else {
			fmt.Printf("  %6d %s\n", e.Size, e.Name)
		}
	}

	return nil
}

func filesRemove(remotePath, device string) error {
	// prepend storage prefix
	remotePath = filesPrefix() + remotePath

	// open device
	man, err := filesOpenDevice(device)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// remove path
	err = man.UseSession(func(s *msg.Session) error {
		return msg.RemovePath(s, remotePath, time.Second*5)
	})
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Removed: %s\n", remotePath)

	return nil
}
