// particles:  Simple particles example using the OpenVG Testbed
// via Nick Williams (github.com/nilliams)

package main

import (
	"flag"
	"github.com/ajstarks/openvg"
	"math/rand"
	"time"
)

const (
	maxrand = 1<<31 - 1
)

type particle struct {
	r, g, b uint8
	x, y    openvg.VGfloat
	vx, vy  openvg.VGfloat
	radius  openvg.VGfloat
}

var (
	particles                                        []particle
	nparticles                                       int
	showTrails, directionRTL, alternate, right, left bool
)

// Initialize _all_ the particles
func initParticles(w, h openvg.VGfloat) {
	particles = make([]particle, nparticles)
	for i := 0; i < nparticles; i++ {
		particles[i].x = 0
		particles[i].y = 0
		particles[i].vx = openvg.VGfloat(rand.Intn(maxrand)%30) + 30
		particles[i].vy = openvg.VGfloat(rand.Intn(maxrand)%20) + 40
		particles[i].r = uint8(rand.Intn(255))
		particles[i].g = uint8(rand.Intn(255))
		particles[i].b = uint8(rand.Intn(255))
		particles[i].radius = openvg.VGfloat(rand.Intn(maxrand)%20) + 20

		if directionRTL {
			particles[i].vx *= -1
			particles[i].x = w
		}
	}
}

func paintBG(w, h openvg.VGfloat) {
	if !showTrails {
		openvg.Background(0, 0, 0)
		return
	}
	openvg.FillRGB(0, 0, 0, 0.3)
	openvg.Rect(0, 0, w, h)
}

func draw(w, h openvg.VGfloat) {
	paintBG(w, h)

	var p particle
	for i := 0; i < nparticles; i++ {
		p = particles[i]
		openvg.FillRGB(p.r, p.g, p.b, 1)
		openvg.Circle(p.x, p.y, p.radius)

		// Apply the velocty
		p.x += p.vx
		p.y += p.vy

		p.vx *= 0.98
		if p.vy > 0 {
			p.vy *= 0.97
		}

		// Gravty
		p.vy -= 0.5

		// Stop p leavng the canvas
		if p.x < -50 {
			p.x = w + 50
		}
		if p.x > w+50 {
			p.x = -50
		}

		// When particle reaches the bottom of screen reset velocity & start posn
		if p.y < -50 {
			p.x = 0
			p.y = 0
			p.vx = openvg.VGfloat(rand.Intn(maxrand)%30) + 30
			p.vy = openvg.VGfloat(rand.Intn(maxrand)%20) + 40

			if directionRTL {
				p.vx *= -1
				p.x = w
			}
		}

		if p.y > h+50 {
			p.y = -50
		}
		particles[i] = p
	}
	openvg.End()
}

func main() {
	flag.BoolVar(&right, "r", false, "right to left")
	flag.BoolVar(&left, "l", false, "left to right")
	flag.BoolVar(&showTrails, "t", false, "show trails")
	flag.IntVar(&nparticles, "n", 25, "number of particles")
	flag.Parse()
	alternate = true
	if right || left {
		alternate = false
	}
	if right {
		directionRTL = true
	}
	if left {
		directionRTL = false
	}
	rand.Seed(time.Now().Unix())
	w, h := openvg.Init()
	width, height := openvg.VGfloat(w), openvg.VGfloat(h)
	initParticles(width, height)
	openvg.Start(w, h)
	i := 0
	for {
		draw(width, height)
		i++
		if alternate && i == 100 { // switch launch direction every 100 draws
			directionRTL = !directionRTL
			i = 0
		}
	}
}