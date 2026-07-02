/**
 * DigiDollar Presentation Framework
 * Keyboard and click navigation for slide presentations
 */

class Presentation {
    constructor(options = {}) {
        this.currentSlide = 0;
        this.slides = document.querySelectorAll('.slide');
        this.totalSlides = this.slides.length;
        this.isAnimating = false;
        this.animationDuration = options.animationDuration || 800;

        this.init();
    }

    init() {
        // Set first slide as active
        if (this.slides.length > 0) {
            this.slides[0].classList.add('active');
        }

        // Create UI elements
        this.createProgressBar();
        this.createSlideCounter();
        this.createNavButtons();
        this.createNavHint();

        // Bind events
        this.bindKeyboardEvents();
        this.bindClickEvents();
        this.bindTouchEvents();

        // Update UI
        this.updateUI();

        // Log ready
        console.log(`Presentation ready: ${this.totalSlides} slides`);
    }

    createProgressBar() {
        const progressBar = document.createElement('div');
        progressBar.className = 'progress-bar';
        progressBar.id = 'progress-bar';
        document.body.appendChild(progressBar);
    }

    createSlideCounter() {
        const counter = document.createElement('div');
        counter.className = 'slide-counter';
        counter.id = 'slide-counter';
        document.body.appendChild(counter);
    }

    createNavButtons() {
        const navContainer = document.createElement('div');
        navContainer.className = 'nav-buttons';

        const prevBtn = document.createElement('button');
        prevBtn.className = 'nav-btn';
        prevBtn.innerHTML = '&#8592;';
        prevBtn.onclick = () => this.prevSlide();

        const nextBtn = document.createElement('button');
        nextBtn.className = 'nav-btn';
        nextBtn.innerHTML = '&#8594;';
        nextBtn.onclick = () => this.nextSlide();

        navContainer.appendChild(prevBtn);
        navContainer.appendChild(nextBtn);
        document.body.appendChild(navContainer);
    }

    createNavHint() {
        const hint = document.createElement('div');
        hint.className = 'nav-hint';
        hint.innerHTML = 'Use <kbd>&#8592;</kbd> <kbd>&#8594;</kbd> or <kbd>Space</kbd> to navigate';
        document.body.appendChild(hint);

        // Fade out hint after 5 seconds
        setTimeout(() => {
            hint.style.opacity = '0';
            hint.style.transition = 'opacity 1s';
        }, 5000);
    }

    bindKeyboardEvents() {
        document.addEventListener('keydown', (e) => {
            switch (e.key) {
                case 'ArrowRight':
                case ' ':
                case 'PageDown':
                    e.preventDefault();
                    this.nextSlide();
                    break;
                case 'ArrowLeft':
                case 'PageUp':
                    e.preventDefault();
                    this.prevSlide();
                    break;
                case 'Home':
                    e.preventDefault();
                    this.goToSlide(0);
                    break;
                case 'End':
                    e.preventDefault();
                    this.goToSlide(this.totalSlides - 1);
                    break;
                case 'f':
                case 'F':
                    this.toggleFullscreen();
                    break;
            }
        });
    }

    bindClickEvents() {
        // Click on right side to advance, left side to go back
        document.addEventListener('click', (e) => {
            // Ignore clicks on buttons or interactive elements
            if (e.target.closest('button, a, input, .nav-buttons')) return;

            const clickX = e.clientX;
            const windowWidth = window.innerWidth;

            if (clickX > windowWidth * 0.7) {
                this.nextSlide();
            } else if (clickX < windowWidth * 0.3) {
                this.prevSlide();
            }
        });
    }

    bindTouchEvents() {
        let touchStartX = 0;
        let touchEndX = 0;

        document.addEventListener('touchstart', (e) => {
            touchStartX = e.changedTouches[0].screenX;
        }, { passive: true });

        document.addEventListener('touchend', (e) => {
            touchEndX = e.changedTouches[0].screenX;
            this.handleSwipe(touchStartX, touchEndX);
        }, { passive: true });
    }

    handleSwipe(startX, endX) {
        const threshold = 50;
        const diff = startX - endX;

        if (Math.abs(diff) > threshold) {
            if (diff > 0) {
                this.nextSlide();
            } else {
                this.prevSlide();
            }
        }
    }

    nextSlide() {
        if (this.isAnimating) return;
        if (this.currentSlide < this.totalSlides - 1) {
            this.goToSlide(this.currentSlide + 1);
        }
    }

    prevSlide() {
        if (this.isAnimating) return;
        if (this.currentSlide > 0) {
            this.goToSlide(this.currentSlide - 1);
        }
    }

    goToSlide(index) {
        if (index < 0 || index >= this.totalSlides || index === this.currentSlide) return;
        if (this.isAnimating) return;

        this.isAnimating = true;

        const oldSlide = this.slides[this.currentSlide];
        const newSlide = this.slides[index];

        // Determine direction
        const goingForward = index > this.currentSlide;

        // Remove active from old slide
        oldSlide.classList.remove('active');

        if (goingForward) {
            // Going forward: old slide goes left (prev), new slide comes from right
            oldSlide.classList.add('prev');
            newSlide.classList.remove('prev'); // Ensure it comes from right
        } else {
            // Going backward: old slide goes right, new slide comes from left (prev position)
            oldSlide.classList.remove('prev');
            newSlide.classList.add('prev'); // Position it on the left first
            // Force reflow to apply the prev position before transition
            newSlide.offsetHeight;
            newSlide.classList.remove('prev'); // Then remove it so it slides in
        }

        newSlide.classList.add('active');

        // Update current slide
        this.currentSlide = index;

        // Update UI
        this.updateUI();

        // Reset animation lock
        setTimeout(() => {
            this.isAnimating = false;
        }, this.animationDuration);
    }

    updateUI() {
        // Update progress bar
        const progress = ((this.currentSlide + 1) / this.totalSlides) * 100;
        const progressBar = document.getElementById('progress-bar');
        if (progressBar) {
            progressBar.style.width = `${progress}%`;
        }

        // Update counter
        const counter = document.getElementById('slide-counter');
        if (counter) {
            counter.textContent = `${this.currentSlide + 1} / ${this.totalSlides}`;
        }
    }

    toggleFullscreen() {
        if (!document.fullscreenElement) {
            document.documentElement.requestFullscreen().catch(err => {
                console.log('Fullscreen not available:', err);
            });
        } else {
            document.exitFullscreen();
        }
    }
}

// Auto-initialize when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.presentation = new Presentation();
});

// Utility function for creating animated counters
function animateCounter(element, target, duration = 2000) {
    const start = 0;
    const startTime = performance.now();

    function update(currentTime) {
        const elapsed = currentTime - startTime;
        const progress = Math.min(elapsed / duration, 1);

        // Easing function (ease-out)
        const easeOut = 1 - Math.pow(1 - progress, 3);
        const current = Math.round(start + (target - start) * easeOut);

        element.textContent = current.toLocaleString();

        if (progress < 1) {
            requestAnimationFrame(update);
        }
    }

    requestAnimationFrame(update);
}

// Utility function for typewriter effect
function typewriter(element, text, speed = 50) {
    let i = 0;
    element.textContent = '';

    function type() {
        if (i < text.length) {
            element.textContent += text.charAt(i);
            i++;
            setTimeout(type, speed);
        }
    }

    type();
}

// Intersection Observer for triggering animations when slides become visible
function initSlideAnimations() {
    const observer = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                entry.target.classList.add('animate-in');
            }
        });
    }, { threshold: 0.5 });

    document.querySelectorAll('.slide').forEach(slide => {
        observer.observe(slide);
    });
}
