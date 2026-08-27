# Simulated robot helpers. `make reset` reseeds the flat-file world; `make demo`
# prints the invocation (needs a real model in a copied config).
.PHONY: reset demo clean

reset:
	@mkdir -p state
	@echo 0   > state/pose_x
	@echo 0   > state/pose_y
	@echo 0   > state/heading
	@echo 100 > state/battery
	@echo 0   > state/obstacle
	@rm -f actuation.log sound.log
	@echo "world reset: pose (0,0), battery 100, no obstacle"

demo:
	@echo "1. cp config.example.json config.json  # add your model block"
	@echo "2. chmod +x devices/*.sh"
	@echo "3. jichi --config config.json doctor   # review kinetic lints"
	@echo "4. jichi --config config.json --auto \\"
	@echo "        --budget-tokens 200k --deadline 10m \\"
	@echo "        -p \"\$$(cat missions/patrol.md)\""
	@echo "   (kinetic tools will ASK; use --kinetic-commands allow only when"
	@echo "    you are watching, or allowlist stop_all and run unattended.)"

clean:
	@rm -f actuation.log sound.log report.md field-notes.jsonl
	@rm -f state/arm_* state/led
