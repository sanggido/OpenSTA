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

#ifndef STA_CLOCK_GATING_CHECK_H
#define STA_CLOCK_GATING_CHECK_H

#include "DisallowCopyAssign.hh"
#include "SdcClass.hh"
#include "RiseFallMinMax.hh"

namespace sta {

class ClockGatingCheck
{
public:
  ClockGatingCheck();
  RiseFallMinMax *margins() { return &margins_; }
  void setActiveValue(LogicValue value);
  LogicValue activeValue() const { return active_value_; }

private:
  DISALLOW_COPY_AND_ASSIGN(ClockGatingCheck);

  RiseFallMinMax margins_;
  LogicValue active_value_;
};

} // namespace
#endif
