// Copyright 2025 Matthew P. Dargan and Justin Rubek. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Media-server is a simple HTTP server that serves a DASH video player.
package main

import (
	"html/template"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
)

var (
	indexTmpl  = template.Must(template.ParseFiles(filepath.Join("templates", "index.html")))
	playerTmpl = template.Must(template.ParseFiles(filepath.Join("templates", "player.html")))
)

func main() {
	http.HandleFunc("/", listFiles)
	http.HandleFunc("/play", playFile)
	http.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.Dir("static"))))
	_ = http.ListenAndServe(":8080", nil)
}

func listFiles(w http.ResponseWriter, r *http.Request) {
	dir := r.URL.Query().Get("dir")
	if dir == "" {
		dir = "."
	}
	fs, err := os.ReadDir(dir)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	data := struct {
		Files []os.DirEntry
		Dir   string
	}{
		Files: fs,
		Dir:   dir,
	}
	if err = indexTmpl.Execute(w, data); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}

func playFile(w http.ResponseWriter, r *http.Request) {
	file := r.URL.Query().Get("file")
	if file == "" {
		http.Error(w, "File not specified", http.StatusBadRequest)
		return
	}
	outputDir := filepath.Join("static", "media")
	if err := os.MkdirAll(outputDir, os.ModePerm); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	outputFile := filepath.Join(outputDir, filepath.Base(file)+".mpd")
	cmd := exec.Command("ffmpeg", "-i", file, "-f", "dash", outputFile)
	if err := cmd.Run(); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	data := struct {
		URL string
	}{
		URL: "/" + outputFile,
	}
	if err := playerTmpl.Execute(w, data); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}
