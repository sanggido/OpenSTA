// OpenSTA, Static Timing Analyzer
// Copyright (c) 2018, Parallax Software, Inc.
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#ifndef STA_TIMING_MODEL_H
#define STA_TIMING_MODEL_H

#include <string>
#include "LibertyClass.hh"

namespace sta {

using std::string;

// Abstract base class for timing models.
class TimingModel
{
public:
  virtual ~TimingModel() {}

protected:
  virtual void setIsScaled(bool is_scaled) = 0;

  friend class LibertyCell;
};

// Abstract base class for gate timing models.
class GateTimingModel : public TimingModel
{
public:
  // Gate delay calculation.
  virtual void gateDelay(const LibertyCell *cell,
			 const Pvt *pvt,
			 float in_slew,
			 float load_cap,
			 float related_out_cap,
			 // Return values.
			 float &gate_delay,
			 float &drvr_slew) const = 0;
  virtual void reportGateDelay(const LibertyCell *cell,
			       const Pvt *pvt,
			       float in_slew,
			       float load_cap,
			       float related_out_cap,
			       int digits,
			       string *result) const = 0;
  virtual float driveResistance(const LibertyCell *cell,
				const Pvt *pvt) const = 0;
};

// Abstract base class for timing check timing models.
class CheckTimingModel : public TimingModel
{
public:
  // Timing check margin delay calculation.
  virtual float checkDelay(const LibertyCell *cell,
			   const Pvt *pvt,
			   float from_slew,
			   float to_slew,
			   float related_out_cap) const = 0;
  virtual void reportCheckDelay(const LibertyCell *cell,
				const Pvt *pvt,
				float from_slew,
				const char *from_slew_annotation,
				float to_slew,
				float related_out_cap,
				int digits,
				string *result) const = 0;
};

} // namespace
#endif
