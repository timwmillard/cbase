
all:
	$(MAKE) -C lib
	$(MAKE) -C base
	$(MAKE) -C base_ecewo
	$(MAKE) -C base_imgui
	$(MAKE) -C base_nanovg
	$(MAKE) -C base_nuklear
	$(MAKE) -C base_raylib
	$(MAKE) -C base_sokol

clean:
	$(MAKE) -C lib clean
	$(MAKE) -C base clean
	$(MAKE) -C base_ecewo clean
	$(MAKE) -C base_imgui clean
	$(MAKE) -C base_nanovg clean
	$(MAKE) -C base_nuklear clean
	$(MAKE) -C base_raylib clean
	$(MAKE) -C base_sokol clean

