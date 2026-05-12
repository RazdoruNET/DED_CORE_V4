# Beginner's Guide - Ignition Basics

## What is Ignition?

Imagine your engine is a cannon. To fire (make a power stroke), you need a spark at the right moment. Ignition is the system that creates that spark.

### In simple terms:
- **Fuel** enters the cylinder
- **Piston** compresses the fuel
- **Spark** ignites the fuel
- **Explosion** pushes the piston down
- **Crankshaft** rotates and moves the motorcycle

## Why is Ignition Timing Important?

### Too early spark:
- Spark appears while piston is still going up
- Engine fights against itself
- Power loss and possible damage

### Too late spark:
- Piston is already going down when spark appears
- Fuel doesn't have time to burn completely
- Engine lacks power, overheats

### Perfect timing:
- Spark appears slightly before top dead center
- Fuel burns when piston is at top dead center
- Maximum pressure pushes piston down

## What is Ignition Advance Angle?

**Ignition advance angle** is how many degrees before top dead center the spark is delivered.

- **Low RPM** - more time needed for combustion → larger angle
- **High RPM** - combustion time doesn't change → smaller angle
- **Load** - with open throttle needs more time → correction

## How DED_CORE_V4 Works?

### Main components:
1. **Crankshaft position sensor** - tells where the piston is
2. **Throttle sensor** - tells how much gas you're giving
3. **ESP32** - smart brain calculates everything
4. **Ignition coil** - creates the spark

### Working algorithm:
1. Sensor says: "crankshaft at position X"
2. ESP32 looks at: "current RPM Y, throttle Z%"
3. ESP32 checks table: "for these conditions need angle A"
4. ESP32 waits for the right moment
5. ESP32 gives command: "deliver spark now"
6. Coil creates spark at perfect moment

## What are Ignition Maps?

**Ignition map** is a table that records what ignition angle is needed for different RPMs.

### Example simple map:
| RPM    | Ignition Angle |
|--------|---------------|
| 1000   | 15°           |
| 2000   | 20°           |
| 3000   | 25°           |
| 4000   | 30°           |
| 5000   | 28°           |

### Why different maps?
- **Map 1** - for city, fuel economy
- **Map 2** - for track, maximum power
- **Map 3** - for bad fuel, safety

## What is Rev Limiter?

**Rev limiter** is engine protection from too high RPMs.

### How it works:
1. Sensor sees: "RPM too high!"
2. System reduces ignition angle
3. Power drops, RPMs don't increase
4. Engine doesn't break

### Types of rev limiters in DED_CORE_V4:
1. **Main limiter** - general protection (e.g., 10000 RPM)
2. **Map 1 limiter** - individual for map 1
3. **Map 2 limiter** - individual for map 2
4. **Map 3 limiter** - individual for map 3

## How to Get Started?

### Step 1: Connection
- Connect sensors to ESP32
- Connect ignition coil
- Power up the system

### Step 2: First startup
- Turn on motorcycle ignition
- Connect to WiFi "DED_BOX_V4"
- Open browser at 192.168.4.1

### Step 3: Basic setup
1. Select map type (7 points for start)
2. Set initial angle (usually 35-45°)
3. Set rev limiter (e.g., 10000 RPM)
4. Save settings

### Step 4: Testing
1. Start the engine
2. Look at idle RPMs
3. Listen to engine operation
4. If runs smoothly - good!

## What to Watch in Web Interface?

### Main parameters:
- **RPM** - current engine RPMs
- **Ignition Angle** - current degrees
- **Throttle** - throttle opening percentage
- **Active Map** - which profile is active

### Indicators:
- **Green** - everything normal
- **Yellow** - attention, check parameters
- **Red** - danger, rev limiter activated

## Simple Safety Rules

### Never do:
- Set angle over 60° at low RPMs
- Disable rev limiter completely
- Ignore red indicators
- Tune on hot engine

### Always do:
- Start with conservative angles
- Check settings at idle
- Listen to engine operation
- Save working settings

## Common Beginner Problems

### Engine won't start:
- Check sensor connections
- Check initial angle (not too small?)
- Check system power

### Engine runs rough:
- Check RPM filter (maybe too large?)
- Check throttle correction
- Check spark plug condition

### No power:
- Ignition angle too late
- Rev limiter activates too early
- Wrong map type for your engine

### Engine knocking:
- Ignition angle too early
- Bad fuel (need to reduce angle)
- Crankshaft sensor problems

## What's Next?

After mastering basics you can:
- Tune precise maps for your engine
- Add temperature correction
- Tune quick shifter
- Use advanced analysis functions

## Remember!

Main beginner rule: **better late than early**. Late ignition just reduces power, while early ignition can break the engine.

Start with conservative settings, gradually improve results, and always listen to how your engine works.

---

Good luck mastering the ignition system! Start simple, gradually move to complex.
