# Navigator
The navigation computer is not a raspberryPI with OpenNavigator, rather it is an Orange Pi Zero 2W.  The reason for this is simply one of power consumption.  The Orange Pi has just as much CPU as the Raspberry PI 4 Plus and just as much RAM, but consumes 25% the power.


## How to use this image
You will want to copy this image to the SD card for the OrangePi follow these steps:
1. Insert the SD card into a card reader and attach to the computer
2. Find the device identifier for the SD card. On a mac you can do this with:
```
diskutil list
```
3. Unmount the device
```
diskutil unmountDisk /dev/disk2
```
4. Copy the image to the device
```
sudo dd of=/dev/disk2 if=orangepi-2024-03-28.img
```

## How to create this image (ie how to backup the Pi)
The steps are identical to how to use the image except for the copy is the reverse:
```
sudo dd if=/dev/disk2 of=orangepi-2024-03-28.img
```

