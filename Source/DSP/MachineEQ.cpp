#include "MachineEQ.h"

namespace TapeMachine
{

MachineEQ::MachineEQ()
{
    setMachine(Machine::Ampex);
    updateCoefficients();
}

void MachineEQ::setSampleRate(double sampleRate)
{
    fs = sampleRate;
    updateCoefficients();
}

void MachineEQ::setMachine(Machine machine)
{
    currentMachine = machine;
}

void MachineEQ::reset()
{
    // Reset Ampex filters
    ampexHP.reset();
    ampexBell1.reset();
    ampexBell2.reset();
    ampexBell3.reset();
    ampexBell4.reset();
    ampexBell5.reset();
    ampexBell6.reset();
    ampexBell7.reset();
    ampexBell8.reset();
    ampexBell9.reset();
    ampexBell10.reset();
    ampexLP.reset();

    // Reset Studer filters
    studerHP1.reset();
    studerHP2.reset();
    studerBell1.reset();
    studerBell2.reset();
    studerBell3.reset();
    studerBell4.reset();
    studerBell5.reset();
    studerBell6.reset();
    studerBell7.reset();
    studerBell8.reset();
    studerBell9.reset();
}

void MachineEQ::updateCoefficients()
{
    // === Ampex ATR-102 "Master" EQ ===
    // Targets: 15Hz=-1.5dB, 20Hz=-1.2dB, 28Hz=0, 40Hz=+1.1dB, 70Hz=+0.15dB,
    // 105Hz=+0.3dB, 150Hz=0, 250Hz=-0.1dB, 1kHz=+0.1dB, 5.5kHz=-0.25dB,
    // 10.5kHz=0, 15kHz=+0.15dB, 20kHz=0, 30kHz=-3dB
    // Optimized parameters: RMS error 0.03dB
    ampexHP.setHighPass(16.0, 0.7071, fs);       // HP @ 16Hz
    ampexBell1.setBell(15.0, 6.0, 2.0, fs);      // Tight LF lift
    ampexBell2.setBell(40.0, 2.0, 1.2, fs);      // Head bump @ 40Hz
    ampexBell3.setBell(75.0, 2.0, -0.1, fs);     // -0.1dB @ 75Hz
    ampexBell4.setBell(100.0, 2.0, 0.3, fs);     // +0.3dB @ 100Hz
    ampexBell5.setBell(150.0, 2.0, 0.0, fs);     // Disabled
    // Midrange
    ampexBell6.setBell(250.0, 2.0, -0.1, fs);    // -0.1dB @ 250Hz
    ampexBell7.setBell(1000.0, 1.5, 0.1, fs);    // +0.1dB @ 1kHz
    ampexBell8.setBell(5500.0, 1.0, -0.25, fs);  // -0.25dB @ 5.5kHz (trough)
    ampexBell9.setBell(10500.0, 1.5, 0.0, fs);   // Disabled
    // HF
    ampexBell10.setBell(18000.0, 1.0, 0.35, fs); // +0.15dB @ 15kHz (air)
    ampexLP.setLowPass(30000.0, 0.7, fs);        // LP2 @ 30kHz, -3dB

    // === Studer A820 "Tracks" EQ ===
    // Targets from Jack Endino: 20Hz=-9dB, 30Hz=-2dB, 38Hz=0dB, 50Hz=+0.55dB,
    // 70Hz=+0.1dB, 110Hz=+1.2dB, 160Hz=+0.5dB, 200Hz=+0.1dB, 400Hz=+0.1dB,
    // 600Hz=+0.2dB, 2kHz=+0.1dB, 5kHz=+0.5dB, 10kHz=0dB, 20kHz=+0.5dB
    // Optimized parameters: RMS error 0.039dB
    studerHP1.setHighPass(27.0, 1.0, fs);        // 2nd order @ 27Hz
    studerHP2.setHighPass(30.5, fs);             // 1st order @ 30.5Hz (total 18 dB/oct)
    // Head bumps and LF shaping
    studerBell1.setBell(46.0, 1.4, 1.10, fs);    // Head bump @ 46Hz
    studerBell2.setBell(70.0, 2.0, -0.50, fs);   // Dip at 70Hz
    studerBell3.setBell(110.0, 2.0, 1.20, fs);   // Head bump 2 @ 110Hz
    studerBell4.setBell(160.0, 1.5, 0.30, fs);   // Shape at 160Hz
    studerBell5.setBell(200.0, 2.0, -0.30, fs);  // Notch at 200Hz
    // Mid and HF
    studerBell6.setBell(600.0, 1.5, 0.20, fs);   // Mid @ 600Hz
    studerBell7.setBell(5000.0, 1.0, 0.50, fs);  // HF @ 5kHz
    studerBell8.setBell(10000.0, 1.5, -0.25, fs);// Cut @ 10kHz
    studerBell9.setBell(20000.0, 1.0, 0.50, fs); // Air @ 20kHz
}

double MachineEQ::processSample(double input)
{
    double x = input;

    if (currentMachine == Machine::Ampex)
    {
        x = ampexHP.process(x);
        x = ampexBell1.process(x);
        x = ampexBell2.process(x);
        x = ampexBell3.process(x);
        x = ampexBell4.process(x);
        x = ampexBell5.process(x);
        x = ampexBell6.process(x);
        x = ampexBell7.process(x);
        x = ampexBell8.process(x);
        x = ampexBell9.process(x);
        x = ampexBell10.process(x);
        x = ampexLP.process(x);
    }
    else
    {
        x = studerHP1.process(x);
        x = studerHP2.process(x);
        x = studerBell1.process(x);
        x = studerBell2.process(x);
        x = studerBell3.process(x);
        x = studerBell4.process(x);
        x = studerBell5.process(x);
        x = studerBell6.process(x);
        x = studerBell7.process(x);
        x = studerBell8.process(x);
        x = studerBell9.process(x);
    }

    return x;
}

} // namespace TapeMachine
