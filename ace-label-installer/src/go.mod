module u1acelabel

go 1.22

require (
	github.com/pwiecz/go-fltk v0.0.0-20260522213023-3e944122e7b1
	golang.org/x/crypto v0.33.0
)

require golang.org/x/sys v0.30.0 // indirect

replace github.com/pwiecz/go-fltk => ../deps/go-fltk-local
