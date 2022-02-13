# scd40fs
A Plan9 / 9Front file system for the scd40 co2 sensor

Writen to work with this sensor sold by adafruit
https://www.adafruit.com/product/5187
manufactures website
https://sensirion.com/products/catalog/SCD40/

Usage;
scd40fs -m mountpoint -s srvname

if no arguments are given, the default mount point is /mnt and the default srv name, and name of the directory, is scd40

the mounted directory will contain 4 files
"all"  lists the CO₂, temperature in celcius, and relative humidity, in a readable format.
"CO₂", "tempC", and "RH" each output their respective data as just a number.
