from bleak import BleakClient, BleakScanner
import asyncio, os, sys

uuids = [
    "19b10000-e8f2-537e-4f6c-d104768a1214",
    "19b10001-e8f2-537e-4f6c-d104768a1214",   # notify
    "19b10002-e8f2-537e-4f6c-d104768a1214",   # write
]

# List of possible names to look for
POSSIBLE_NAMES = ["PicoBLE"]

async def find_pico_device():
    """Scan for any device that matches our possible names"""
    print("Scanning for Pico devices...")
    
    devices = await BleakScanner.discover(timeout=10.0)
    print(f"Found {len(devices)} total devices:")
    
    # Show all devices for debugging
    for device in devices:
        print(f"  {device.name or 'Unknown'} ({device.address})")
    
    # Look for our Pico device
    for name in POSSIBLE_NAMES:
        for device in devices:
            if device.name == name:
                print(f"\nFound Pico device: {device.name} ({device.address})")
                return device
    
    print(f"\nNo Pico device found. Looking for: {POSSIBLE_NAMES}")
    return None

async def main():
    # Find the device
    device = await find_pico_device()
    if device is None:
        print("Device not found")
        return

    print(f"\nConnecting to {device.name}...")
    
    try:
        async with BleakClient(device) as client:
            print(f"Connected to {device.name}")
            
            # Discover and print all services
            print("Discovering services...")
            services = client.services
            for service in services:
                print(f"Service: {service.uuid}")
                for char in service.characteristics:
                    print(f"  Characteristic: {char.uuid} - Properties: {char.properties}")
            
            # Try to find our custom service and characteristics
            custom_service = None
            notify_char = None
            write_char = None
            
            # Look for our custom service UUID
            for service in services:
                if service.uuid.lower() == uuids[0].lower():
                    custom_service = service
                    print(f"Found custom service: {service.uuid}")
                    break
            
            if not custom_service:
                print("Custom service not found! Available services:")
                for service in services:
                    print(f"  {service.uuid}")
                return
            
            # Find the characteristics
            for char in custom_service.characteristics:
                if char.uuid.lower() == uuids[1].lower():
                    notify_char = char
                    print(f"Found notify characteristic: {char.uuid}")
                elif char.uuid.lower() == uuids[2].lower():
                    write_char = char
                    print(f"Found write characteristic: {char.uuid}")
            
            if not notify_char or not write_char:
                print("Required characteristics not found!")
                return
            
            # Set up notifications
            await client.start_notify(notify_char.uuid, lambda _, d: print(f"Received: {d.decode().strip()}"))
            
            print("\nCONNECTED. Type your command (ctrl-C to quit).")
            print("Try: 'led on', 'led off', 'led?'")
            
            while True:
                cmd = await asyncio.get_event_loop().run_in_executor(None, input, "> ")
                if not cmd.strip():  # empty line → skip
                    continue
                await client.write_gatt_char(write_char.uuid, cmd.encode(), response=False)
                
    except Exception as e:
        print(f"Connection error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    os.system("cls" if os.name == "nt" else "clear")
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nGoodbye!")