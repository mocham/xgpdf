package main
//#cgo LDFLAGS: -L./Bin -l:libpdf.so
import "C"
import "github.com/mocham/xgpdf"
import "os"
func main() {
	xgpdf.PDFWidget(os.Args...)
}
