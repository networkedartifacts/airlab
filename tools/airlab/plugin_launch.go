package main

import (
	"fmt"
	"time"

	"github.com/256dpi/naos/pkg/msg"
	"github.com/256dpi/naos/pkg/serial"
	"github.com/spf13/cobra"
)

var pluginLaunchMode string

var pluginLaunchCmd = &cobra.Command{
	Use:   "launch <name> [device]",
	Short: "Launch a plugin on a device",
	Args:  cobra.RangeArgs(1, 2),
	RunE: func(cmd *cobra.Command, args []string) error {
		device := ""
		if len(args) > 1 {
			device = args[1]
		}
		return pluginLaunch(args[0], device)
	},
}

func init() {
	pluginLaunchCmd.Flags().StringVarP(&pluginLaunchMode, "mode", "b", "", "Mode to launch the plugin in.")

	pluginCmd.AddCommand(pluginLaunchCmd)
}

func pluginLaunch(name, device string) error {
	// check name
	if name == "" {
		panic("missing name")
	}

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

	// kill running plugin
	err = sess.Send(0xA1, []byte{0x3}, 5*time.Second)
	if err != nil {
		return err
	}

	// prepare command
	cmd := append([]byte{0x2}, []byte(name)...)
	if pluginLaunchMode != "" {
		cmd = append(cmd, 0x0)
		cmd = append(cmd, []byte(pluginLaunchMode)...)
	}

	// launch plugin
	err = sess.Send(0xA1, cmd, 5*time.Second)
	if err != nil {
		return err
	}

	// log
	fmt.Printf("==> Launched!\n")

	// start log streaming
	err = sess.Send(0xA1, []byte{0x4}, 5*time.Second)
	if err != nil {
		return err
	}

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
