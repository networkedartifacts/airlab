package main

import (
	"fmt"
	"time"

	"github.com/256dpi/naos/pkg/msg"
	"github.com/256dpi/naos/pkg/serial"
	"github.com/spf13/cobra"
)

var pluginMonitorCmd = &cobra.Command{
	Use:   "monitor [device]",
	Short: "Stream engine log from a device",
	Args:  cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 0 {
			device = args[0]
		}
		return pluginMonitor(device)
	},
}

func init() {
	pluginCmd.AddCommand(pluginMonitorCmd)
}

func pluginMonitor(device string) error {
	// open device
	var dev msg.Device
	var err error
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

	// create session
	sess, err := man.NewSession()
	if err != nil {
		return err
	}
	defer sess.End(0)

	// start log streaming
	err = sess.Send(0xA1, []byte{0x4}, 5*time.Second)
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Streaming...\n")

	// TODO: Prevent session timeout.

	// receive logs
	for {
		log, err := sess.Receive(0xA1, false, time.Minute)
		if err != nil {
			return err
		}
		fmt.Printf("==> Log: %s\n", string(log))
	}
}
