package main

import (
	"crypto/rand"
	"encoding/hex"
	"path/filepath"
	"time"

	"github.com/256dpi/naos/pkg/msg"
)

func stagedUpload(s *msg.Session, path string, data []byte, report func(uint32), timeout time.Duration) error {
	// generate staging ID
	uid := make([]byte, 6)
	_, err := rand.Read(uid)
	if err != nil {
		return err
	}
	stagingPath := "/int/tmp/" + hex.EncodeToString(uid)

	// ensure directories
	err = msg.MakePath(s, "/int/tmp", time.Second)
	if err != nil {
		return err
	}
	dir := filepath.Dir(path)
	if dir != "" && dir != "/" {
		err = msg.MakePath(s, dir, time.Second)
		if err != nil {
			return err
		}
	}

	// write to staging path
	err = msg.WriteFile(s, stagingPath, data, report, timeout)
	if err != nil {
		return err
	}

	// rename to final path
	return msg.RenamePath(s, stagingPath, path, time.Second)
}
