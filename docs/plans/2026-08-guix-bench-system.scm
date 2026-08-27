;; A HEADLESS Guix System for the jichi hardware-test bench.
;;
;; The published guix-system-vm-image is a live graphical desktop with no sshd
;; (M450). This declares what the row actually needs: sshd, a serial console,
;; and the toolchain.
;;
;; CORRECTION (M466): M450 also said that image "has no serial console, so it
;; cannot be driven", and that half is wrong. GRUB in 1.5.0 both WRITES to and
;; READS FROM the serial line -- the menu renders over `-serial mon:stdio` and a
;; keypress opens the entry editor. Only the KERNEL is un-told (`quiet`, no
;; `console=`), which is one argument rather than a dead end. What actually
;; resists is GRUB's serial INPUT reliability: four attempts, and the decisive
;; error was `unspecified search type` -- GRUB parsing a bare `search` because
;; the `--fs-uuid` token never arrived. Dropped bytes, not a wrong command.
;; Measurements, the extracted boot entry, and the three ranked next moves:
;; docs/analysis/2026-08-17-driving-the-published-guix-image.md
(use-modules (gnu) (gnu packages) (srfi srfi-1))
(use-service-modules networking ssh)
(use-package-modules base commencement version-control curl pkg-config certs)

;; The key authorized in the guest is a property of the MACHINE running this
;; build, not of this file.  Hardcoding /home/<user>/.ssh/... made the one
;; platform whose row cannot rebuild itself unrebuildable on every host but
;; the one that wrote the file -- see docs/PLATFORMS.md (Guix) and the
;; --ref-secs pattern in scripts/tier-b-device.sh, which is the same rule:
;; a rig refuses rather than guesses.  Defaulting to some key would be worse
;; than refusing here, because the wrong guess is an authorized credential.
(define %bench-pubkey
  (let ((p (getenv "JICHI_BENCH_PUBKEY")))
    (cond ((or (not p) (string=? p ""))
           (error (string-append
                   "JICHI_BENCH_PUBKEY is unset.  Point it at the ssh public "
                   "key to authorize in the guest, e.g.\n"
                   "  export JICHI_BENCH_PUBKEY="
                   "$HOME/.ssh/id_ed25519_jichi_bench.pub")))
          ((not (char=? (string-ref p 0) #\/))
           (error "JICHI_BENCH_PUBKEY must be an absolute path:" p))
          ((not (file-exists? p))
           (error "JICHI_BENCH_PUBKEY does not exist:" p))
          (else p))))

(operating-system
  (host-name "guix-bench")
  (timezone "Europe/Berlin")
  (locale "en_US.utf8")

  ;; Serial console, so the boot is observable without a display.
  (kernel-arguments '("console=ttyS0,115200n8"))

  (bootloader (bootloader-configuration
               (bootloader grub-bootloader)
               (targets '("/dev/vda"))
               (terminal-outputs '(console serial))))

  (file-systems (cons (file-system
                       (device (file-system-label "guix-root"))
                       (mount-point "/")
                       (type "ext4"))
                      %base-file-systems))

  (users (cons (user-account
                (name "bench")
                (group "users")
                (supplementary-groups '("wheel"))
                (home-directory "/home/bench"))
               %base-user-accounts))

  ;; The gate needs: a C toolchain, make, libcurl + pkg-config, git, coreutils.
  (packages (append (list gcc-toolchain gnu-make curl pkg-config git nss-certs)
                    %base-packages))

  (services (append (list (service dhcp-client-service-type)
                          (service openssh-service-type
                                   (openssh-configuration
                                    (permit-root-login #t)
                                    (password-authentication? #f)
                                    (authorized-keys
                                     `(("bench" ,(local-file %bench-pubkey))
                                       ("root"  ,(local-file %bench-pubkey))))))))
                    %base-services)))
