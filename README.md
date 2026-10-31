# Original XBOX SMC reimplmentation

Original XBOX SMC implemented with a Rapsberry Pico.

Very very experimental, currently it boots on 1.0 boards.

## Whats working
- Power ON/OFF
- LED control
- AV pack detection/override
- DVD eject
- Fan control
- SMC boot challenge/override
- Logging via uart
- Experimental WiFi control, requires external power to the pico (3.3v standby rail is not strong enough)

## Notes
- *Heavily* based on the work Prehistoricman did. [Xbox_SMC](https://github.com/Prehistoricman/Xbox_SMC)
- Some AI assistance was used (claude), mostly for the web interface.
