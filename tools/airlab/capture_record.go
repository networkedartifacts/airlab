package main

import (
	"fmt"
	"os"
	"os/signal"
	"sort"
	"strings"
	"syscall"
	"time"

	"github.com/256dpi/naos/pkg/msg"
	"github.com/spf13/cobra"
)

var captureRecordOnce bool
var captureRecordRaw bool

var captureRecordCmd = &cobra.Command{
	Use:   "record [device]",
	Short: "Record screen captures from a device",
	Args:  cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 0 {
			device = args[0]
		}
		return captureRecord(device)
	},
}

func init() {
	captureRecordCmd.Flags().BoolVar(&captureRecordOnce, "once", false, "Stop after capturing one frame.")
	captureRecordCmd.Flags().BoolVar(&captureRecordRaw, "raw", false, "Save raw .bin files instead of .png images.")

	captureCmd.AddCommand(captureRecordCmd)
}

func captureRecord(device string) error {
	// open device
	man, err := filesOpenDevice(device)
	if err != nil {
		return err
	}
	defer man.Deactivate()

	// set gfx-record param to enable recording
	fmt.Printf("==> Enabling recording...\n")
	err = man.UseSession(func(s *msg.Session) error {
		return msg.SetParam(s, "gfx-record", []byte("1"), time.Second*5)
	})
	if err != nil {
		return err
	}

	// ensure recording is disabled on exit
	defer func() {
		fmt.Printf("==> Disabling recording...\n")
		_ = man.UseSession(func(s *msg.Session) error {
			return msg.SetParam(s, "gfx-record", []byte("0"), time.Second*5)
		})
	}()

	// catch interrupt signal
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)

	// poll and transfer files
	fmt.Printf("==> Recording... (press Ctrl+C to stop)\n")
	total := 0
	for {
		// check for stop signal
		select {
		case <-sig:
			fmt.Printf("==> Recorded %d frames.\n", total)
			return nil
		default:
		}

		// list dump directory
		var entries []msg.FSInfo
		err = man.UseSession(func(s *msg.Session) error {
			var err error
			entries, err = msg.ListDir(s, "/ext/dump", time.Second*5)
			return err
		})
		if err != nil {
			// directory may not exist yet
			time.Sleep(100 * time.Millisecond)
			continue
		}

		// filter and sort screen files
		var files []string
		for _, e := range entries {
			if !e.IsDir && strings.HasPrefix(e.Name, "screen-") && strings.HasSuffix(e.Name, ".bin") {
				files = append(files, e.Name)
			}
		}
		sort.Strings(files)

		// transfer and delete each file
		for _, name := range files {
			remotePath := "/ext/dump/" + name

			// download and delete file
			var data []byte
			err = man.UseSession(func(s *msg.Session) error {
				var err error
				data, err = msg.ReadFile(s, remotePath, nil, time.Minute)
				if err != nil {
					return err
				}
				return msg.RemovePath(s, remotePath, time.Second*5)
			})
			if err != nil {
				fmt.Printf("==> Error transferring %s: %v\n", name, err)
				continue
			}

			// write local file
			if captureRecordRaw {
				err = os.WriteFile(name, data, 0644)
			} else {
				palette := makePalette()
				img := convertImage(data, palette, captureScale)
				err = writePNG(strings.ReplaceAll(name, ".bin", ".png"), img)
			}
			if err != nil {
				return err
			}

			total++
			fmt.Printf("==> Captured: %s (%d bytes)\n", name, len(data))

			// stop after one frame if requested
			if captureRecordOnce {
				fmt.Printf("==> Recorded %d frames.\n", total)
				return nil
			}
		}

		// wait before polling again
		time.Sleep(time.Second)
	}
}
