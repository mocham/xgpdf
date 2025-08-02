package xgpdf
/*
#cgo LDFLAGS: -L./ -l:libpdf.so
#include <stdlib.h>
#include "CPlugins/src/wrapper-pdf.h"
*/
import "C"
import (
	"strings"
	"os"
	"unsafe"
	"regexp"
	"github.com/mocham/xgw"
	"path/filepath"
)
type Window = xgw.Window
var ErrLoad = xgw.ErrLoad
var tocRe = regexp.MustCompile(`(?:appendix|section|subsection|subsubsection|chapter)\*?\d*(?:\.\d*[A-Za-z]*)*$`)
type PDFDocument *C.void
func initPDFFile(filename string) (PDFDocument, string, error) {
	document := C.init_pdf_document((*C.char)(xgw.CStr(filename).Ptr))
	if document == nil { return nil, "", ErrLoad }
	tocCStr := C.get_pdf_toc(document)
	if tocCStr != nil { defer C.free(unsafe.Pointer(tocCStr)) }
	return PDFDocument(document), C.GoString(tocCStr), nil
}
func closePDF(document PDFDocument) { C.cleanup_pdf_document(unsafe.Pointer(document)) }

func LoadPDFPage(document PDFDocument, pageNumber, width, height, xoffset int) (buffer []uint32, ht int, err error) {
	buffer = make([]uint32, 0, width*height)[:width*height]
	if ht = int(C.render_pdf_page_to_rgba_with_xoffset(unsafe.Pointer(document), C.int(pageNumber), xgw.Ptr[C.uchar](&buffer[0]), C.int(width), C.int(height), C.int(xoffset))); ht == 0 { err = ErrLoad }
	return
}

func getPDFSelection(document PDFDocument, pageNumber int, text string) string {
	cStr := C.get_pdf_text_selection(unsafe.Pointer(document), C.int(pageNumber), (*C.char)(xgw.CStr(text).Ptr))
	if cStr == nil { return "" }
	defer C.free(unsafe.Pointer(cStr))
	return C.GoString(cStr)
}

func getPDFText(document PDFDocument, pageNumber int) string {
	cStr := C.extract_page_text(unsafe.Pointer(document), C.int(pageNumber))
	if cStr == nil { return "" }
	defer C.free(unsafe.Pointer(cStr))
	return C.GoString(cStr)
}

func tableOfContentWidget(title, toc string, win Window) {
	duState := xgw.DuState[string] {
		At: func (a, b string) string { if a == "init" { return a }; return "" },
		Render: func (a string, m map[string]string, open bool) string { return a },
	}
	for _, item := range strings.Split(toc, "\n") {
		pair := strings.Split(item, "@")
		if len(pair) < 2 { continue }
		duState.List = append(duState.List, xgw.Pair[string]{Key: tocRe.ReplaceAllString(pair[0], ""), Value: pair[1]})
	}
	xgw.DuWidget[string]("init", "", 0.3, func (a, b string) *xgw.DuState[string] { return &duState }, func (state *xgw.DuState[string], cmd string) (ret string) { 
		if state.Cursor < 0 || state.Cursor >= len(state.List) { return }
		xgw.SetWmName(win, title + "@" + state.List[state.Cursor].Value + "*")
		return
	})
}

func continuousPDFWidget(winWidth, winHeight, pageHeight, xoffset int, title, pdfFile string) {
	if len(pdfFile) < 7 || pdfFile[:7] != "file://" { pdfFile = "file://" + pdfFile }
	document, toc, err := initPDFFile(pdfFile)
	defer func() { if document != nil { closePDF(document) } } ()
	if err != nil { return }
	cache, currentPage, currentTop, numInput, pageGap, positionDelta := map[int][]uint32{}, 0, 0, 0, 30, 200
	gapImg := xgw.RGBAData{ Pix:make([]uint32, winWidth*pageGap), Stride: winWidth*4, Width: winWidth, Height: pageGap }
	load := func (i int) []uint32 {
		if data, exists := cache[i]; exists { return data }
		if data, ht, err := LoadPDFPage(document, i, winWidth, pageHeight, -xoffset); ht > 0 && err == nil { cache[i] = data; return data }
		return nil
	}
	paintPage := func(ximg *xgw.XImage, i, verticalShift int) (dataHeight int) {
		if verticalShift >= pageHeight { return 0 }
		data := xgw.RGBAData{ Pix: load(i), Stride: winWidth*4, Width:winWidth, Height: pageHeight }
		if len(data.Pix) == 0 { return 0 }
		if dataHeight = pageHeight; verticalShift >= 0{
			paintHeight := dataHeight
			if verticalShift + dataHeight > winHeight { paintHeight = dataHeight - verticalShift }
			ximg.XDraw(xgw.Crop(data, 0, 0, winWidth, paintHeight), 0, verticalShift)
		} else if dataHeight + verticalShift > 0{
			paintHeight := dataHeight + verticalShift
			if paintHeight > winHeight { paintHeight = winHeight }
			ximg.XDraw(xgw.Crop(data, 0, -verticalShift, winWidth, paintHeight), 0, 0)
		}
		return dataHeight
	}
	paint := func(ximg *xgw.XImage) (int, int) {
		if currentTop < 0 { currentTop = 0 }
		if currentPage < 0 { currentPage, currentTop = 0, 0 }
		if ht := paintPage(ximg, currentPage, -currentTop); ht >=0 && (ht + pageGap - currentTop < winHeight) {
			ximg.XDraw(gapImg, 0, ht-currentTop)
			paintPage(ximg, currentPage + 1, -currentTop + ht + pageGap)
		}
		return 0, 0
	}
	updatePosition := func (delta int) {
		if currentTop += delta; currentTop > pageHeight {
			currentPage += 1
			currentTop -= pageHeight
		} else if currentTop < 0 {
			load(currentPage - 1)
			currentPage -= 1
			currentTop = pageHeight + currentTop
		}
	}
	button := func (detail byte, x, y int16) int {
		switch detail {
		case 4: updatePosition(-positionDelta)
		case 5: updatePosition(positionDelta)
		default: return 0
		}
		return 1
	}
	refresh := func (str string) {
		if parts := strings.Split(str[:len(str) - 1], "@"); len(parts) < 2 {
			closePDF(document)
			document, _, _ = initPDFFile(pdfFile)
			cache = make(map[int][]uint32)
		} else {
			currentPage, currentTop = xgw.ParseInt(parts[1]), 0
		}
	}
	keypress := func (detail byte) int {
		switch detail {
		case 10, 11, 12, 13, 14, 15, 16, 17, 18, 19: numInput = numInput * 10 + int(detail + 1) % 10; return 0
		case 24: return -1
		case 36: if numInput > 0 { if len(load(numInput)) > 0 { currentPage, currentTop = numInput, 0 }; numInput = 0 } else { return 0 }
		case 30: updatePosition(-3*positionDelta)
		case 40: updatePosition(3*positionDelta)
		case 111: updatePosition(-2*positionDelta)
		case 116: updatePosition(2*positionDelta)
		case 113: updatePosition(-positionDelta)
		case 114: updatePosition(positionDelta)
		default: return 0
		}
		return 1
	}
	init := func (ximg *xgw.XImage) { if strings.Contains(toc, "@") { go tableOfContentWidget(title + "-toc", toc, ximg.Win) } }
	xgw.UniversalWidget(title, 0, 0, winWidth, winHeight, paint, button, keypress, refresh, init)
}

func main() {
	action := os.Args
	defer xgw.Cleanup()
	if len(action) <= 1 { return }
	if len(action) < 6 {
		action = []string{action[1], "1750", "1600", "72", "46"}
	}
	pageWidth, winHeight, aspect, xoffset := xgw.ParseInt(action[1]), xgw.ParseInt(action[2]), xgw.ParseInt(action[3]), xgw.ParseInt(action[4])
	if pageWidth < 100 { pageWidth = xgw.Width*7/10 }
	if winHeight < 100 { winHeight = xgw.Height-40 }
	if aspect < 1 { aspect = 80 }
	continuousPDFWidget(pageWidth, winHeight, int(float64(pageWidth)*100.0/float64(aspect)), xoffset, "auto-pdf-" + filepath.Base(action[0]), action[0])
}
