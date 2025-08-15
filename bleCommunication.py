from bleak import BleakClient, BleakScanner
import asyncio, os
from uuid import UUID

POSSIBLE_NAMES = ["PicoBLE"]
# Use the actual UUIDs that the Pico is advertising
UU_SERVICE = "14128a76-04d1-6c4f-7e53-f2e80000b119"
UU_NOTIFY  = "14128a76-04d1-6c4f-7e53-f2e80100b119"
UU_WRITE   = "14128a76-04d1-6c4f-7e53-f2e80200b119"

# Since we're now using the correct UUIDs, we don't need the both_forms function
NOTIFY_FORMS = {UU_NOTIFY.lower()}
WRITE_FORMS  = {UU_WRITE.lower()}

async def find_pico_device():
    print("Scanning for Pico devices...")
    def filt(d, ad): return (d.name or "") in POSSIBLE_NAMES
    dev = await BleakScanner.find_device_by_filter(filt, timeout=10.0)
    if dev:
        print(f"Found: {dev.name or 'Unknown'} ({dev.address})")
        return dev
    devices = await BleakScanner.discover(timeout=5.0)
    print(f"Found {len(devices)} devices (no match):")
    for d in devices:
        print(f"  {d.name or 'Unknown'} ({d.address})")
    return None

async def wait_for_services(client, timeout=6.0):
    end = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < end:
        svcs = client.services
        if svcs and len(list(svcs)) > 0:
            return svcs
        await asyncio.sleep(0.2)
    return client.services

async def main():
    dev = await find_pico_device()
    if not dev:
        print("Device not found"); return

    print(f"\nConnecting to {dev.name or 'Unknown'}...")
    try:
        async with BleakClient(dev, timeout=15.0) as client:
            if not client.is_connected:
                print("Failed to connect."); return
            print("Connected.")

            # Populate and print services (helps verify UUIDs seen by macOS)
            services = await wait_for_services(client, timeout=6.0)
            print("=== Services/Characteristics discovered ===")
            if services:
                for svc in services:
                    print(f"Service: {svc.uuid}")
                    for ch in svc.characteristics:
                        print(f"  Char: {ch.uuid}  props={ch.properties}")
                        # Check for CCCD (Client Characteristic Configuration Descriptor)
                        for desc in ch.descriptors:
                            print(f"    Desc: {desc.uuid}")
            else:
                print("(no services yet)")

            # Match characteristics against BOTH canonical and byte-reversed UUIDs
            notify_char = write_char = None
            if services:
                for svc in services:
                    for ch in svc.characteristics:
                        u = str(ch.uuid).lower()
                        if u in NOTIFY_FORMS:
                            notify_char = ch
                            print(f"Found notify char: {ch.uuid}")
                        elif u in WRITE_FORMS:
                            write_char = ch
                            print(f"Found write char: {ch.uuid}")

            if not notify_char or not write_char:
                print("Notify/Write characteristics not found among discovered services.")
                print("Reflash firmware and power-cycle both sides if this persists.")
                return

            ready = asyncio.Event()
            response_received = asyncio.Event()
            
            def on_notify(_, data: bytearray):
                try:
                    msg = data.decode(errors="ignore").strip()
                except Exception:
                    msg = repr(data)
                print(f"Received: {msg}")
                response_received.set()
                if msg == "<READY>":
                    ready.set()

            # Start notifications using the characteristic object
            print("Starting notifications...")
            await client.start_notify(notify_char, on_notify)
            print("Notifications enabled.")

            # Wait briefly for handshake
            try:
                await asyncio.wait_for(ready.wait(), timeout=5.0)
                print("Device is READY.")
            except asyncio.TimeoutError:
                print("No <READY> yet; continuing.")

            print("\nType: led on | led off | led? | test  (Ctrl-C to quit)")
            loop = asyncio.get_event_loop()
            while True:
                cmd = await loop.run_in_executor(None, input, "> ")
                if not cmd.strip():
                    continue
                
                print(f"Sending: '{cmd}'")
                response_received.clear()
                
                # Send command
                await client.write_gatt_char(write_char, cmd.encode(), response=True)
                print("Command sent, waiting for response...")
                
                # Wait for response with timeout
                try:
                    await asyncio.wait_for(response_received.wait(), timeout=5.0)
                    print("Response received!")
                except asyncio.TimeoutError:
                    print("No response received within 5 seconds")
                    
                    # Try to read the notification characteristic directly
                    try:
                        if 'read' in notify_char.properties:
                            value = await client.read_gatt_char(notify_char)
                            if value:
                                print(f"Read from notify char: {value}")
                    except Exception as e:
                        print(f"Failed to read notify char: {e}")

    except Exception as e:
        print(f"Connection error: {e}")
        import traceback; traceback.print_exc()

if __name__ == "__main__":
    os.system("cls" if os.name == "nt" else "clear")
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nGoodbye!")