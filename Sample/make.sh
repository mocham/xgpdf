#!/bin/bash

go build -o pdf main.go
patchelf --replace-needed libpdf.so $1/libpdf.so Bin/pdf
