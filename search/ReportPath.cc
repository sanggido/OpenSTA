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

#include "Machine.hh"
#include "Report.hh"
#include "Error.hh"
#include "StringUtil.hh"
#include "Units.hh"
#include "Fuzzy.hh"
#include "TimingRole.hh"
#include "Transition.hh"
#include "PortDirection.hh"
#include "TimingArc.hh"
#include "Liberty.hh"
#include "Network.hh"
#include "PortDelay.hh"
#include "ExceptionPath.hh"
#include "InputDrive.hh"
#include "Sdc.hh"
#include "Graph.hh"
#include "ClkInfo.hh"
#include "Tag.hh"
#include "PathVertex.hh"
#include "DcalcAnalysisPt.hh"
#include "ArcDelayCalc.hh"
#include "GraphDelayCalc.hh"
#include "Parasitics.hh"
#include "PathAnalysisPt.hh"
#include "PathGroup.hh"
#include "CheckMinPulseWidths.hh"
#include "CheckMinPeriods.hh"
#include "CheckMaxSkews.hh"
#include "PathRef.hh"
#include "Search.hh"
#include "PathExpanded.hh"
#include "Latches.hh"
#include "Corner.hh"
#include "Genclks.hh"
#include "ReportPath.hh"

namespace sta {

ReportField::ReportField(const char *name,
			 const char *title,
			 int width,
			 bool left_justify,
			 Unit *unit,
			 bool enabled) :
  name_(name),
  title_(stringCopy(title)),
  left_justify_(left_justify),
  unit_(unit),
  blank_(NULL),
  enabled_(enabled)
{
  setWidth(width);
}

ReportField::~ReportField()
{
  stringDelete(title_);
  stringDelete(blank_);
}

void
ReportField::setProperties(const char *title,
			   int width,
			   bool left_justify)
{
  if (title_)
    stringDelete(title_);
  title_ = stringCopy(title);
  left_justify_ = left_justify;
  setWidth(width);
}


void
ReportField::setWidth(int width)
{
  width_ = width;

  if (blank_)
    stringDelete(blank_);
  blank_ = new char[width + 1];
  int i;
  for (i = 0; i < width; i++)
    blank_[i] = ' ';
  blank_[i] = '\0';
}

void
ReportField::setEnabled(bool enabled)
{
  enabled_ = enabled;
}

////////////////////////////////////////////////////////////////

const float ReportPath::field_blank_ = -1.0;

ReportPath::ReportPath(StaState *sta) :
  StaState(sta),
  start_end_pt_width_(78),
  plus_zero_(NULL),
  minus_zero_(NULL)
{
  makeFields();
  setPathFormat(report_path_full);
  setReportFields(false, false, false, false);
  setDigits(2);
  setNoSplit(false);
}

ReportPath::~ReportPath()
{
  delete fields_;
  delete field_description_;
  delete field_total_;
  delete field_incr_;
  delete field_capacitance_;
  delete field_slew_;
  delete field_fanout_;
  delete field_edge_;
  delete field_case_;

  if (plus_zero_) {
    stringDelete(plus_zero_);
    stringDelete(minus_zero_);
  }
}

void
ReportPath::makeFields()
{
  fields_ = new ReportFieldSeq;
  field_fanout_ = makeField("fanout", "Fanout", 10, false, NULL, true);
  field_capacitance_ = makeField("capacitance", "Cap", 10, false,
				 units_->capacitanceUnit(), true);
  field_slew_ = makeField("slew", "Slew", 10, false, units_->timeUnit(), true);
  field_incr_ = makeField("incr", "Delay", 10, false, units_->timeUnit(), true);
  field_total_ = makeField("total", "Time", 10, false, units_->timeUnit(), true);
  field_edge_ = makeField("edge", "", 1, false, NULL, true);
  field_case_ = makeField("case", "case", 10, false, NULL, false);
  field_description_ = makeField("description", "Description", 36, true, NULL, true);
}

ReportField *
ReportPath::makeField(const char *name,
		      const char *title,
		      int width,
		      bool left_justify,
		      Unit *unit,
		      bool enabled)
{
  ReportField *field = new ReportField(name, title, width, left_justify,
				       unit, enabled);
  fields_->push_back(field);
  return field;
}

ReportField *
ReportPath::findField(const char *name)
{
  ReportFieldSeq::Iterator field_iter(fields_);
  while (field_iter.hasNext()) {
    ReportField *field = field_iter.next();
    if (stringEq(name, field->name()))
	return field;
  }
  return NULL;
}

void
ReportPath::setReportFieldOrder(StringSeq *field_names)
{
  // Disable all fields.
  ReportFieldSeq::Iterator field_iter1(fields_);
  while (field_iter1.hasNext()) {
    ReportField *field = field_iter1.next();
    field->setEnabled(false);
  }

  ReportFieldSeq *next_fields = new ReportFieldSeq;
  StringSeq::Iterator name_iter(field_names);
  while (name_iter.hasNext()) {
    const char *field_name = name_iter.next();
    ReportField *field = findField(field_name);
    next_fields->push_back(field);
    field->setEnabled(true);
  }
  // Push remaining disabled fields on the end.
  ReportFieldSeq::Iterator field_iter2(fields_);
  while (field_iter2.hasNext()) {
    ReportField *field = field_iter2.next();
    if (!field->enabled())
      next_fields->push_back(field);
  }
  delete fields_;
  fields_ = next_fields;
}

void
ReportPath::setReportFields(bool report_input_pin,
			    bool report_net,
			    bool report_cap,
			    bool report_slew)
{
  report_input_pin_ = report_input_pin;
  report_net_ = report_net;

  field_fanout_->setEnabled(report_net_);
  field_capacitance_->setEnabled(report_cap);
  field_slew_->setEnabled(report_slew);
  // for debug
  field_case_->setEnabled(false);
}

void
ReportPath::setPathFormat(ReportPathFormat format)
{
  format_ = format;  
}

void
ReportPath::setNoSplit(bool no_split)
{
  no_split_ = no_split;
}

void
ReportPath::setDigits(int digits)
{
  digits_ = digits;

  if (plus_zero_) {
    stringDelete(plus_zero_);
    stringDelete(minus_zero_);
  }
  minus_zero_ = stringPrint(digits_ + 4, "-%.*f", digits_, 0.0);
  plus_zero_  = stringPrint(digits_ + 3,  "%.*f", digits_, 0.0);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportPathEnd(PathEnd *end)
{
  string header, result;
  switch (format_) {
  case report_path_full:
  case report_path_full_clock:
  case report_path_full_clock_expanded:
    end->reportFull(this, result);
    report_->print(result);
    report_->print("\n\n");
    break;
  case report_path_short:
    end->reportShort(this, result);
    report_->print(result);
    report_->print("\n\n");
    break;
  case report_path_end:
    reportEndHeader(header);
    report_->print(header);
    reportEndLine(end, result);
    report_->print(result);
    break;
  case report_path_summary:
    reportSummaryHeader(header);
    report_->print(header);
    reportSummaryLine(end, result);
    report_->print(result);
    break;
  case report_path_slack_only:
    reportSlackOnlyHeader(header);
    report_->print(header);
    reportSlackOnly(end, result);
    report_->print(result);
    report_->print("\n");
    break;
  default:
    internalError("unsupported path type");
    break;
  }
}

void
ReportPath::reportPathEnds(PathEndSeq *ends)
{
  if (!ends->empty()) {
    string header;
    PathEndSeq::Iterator end_iter(ends);
    switch (format_) {
    case report_path_full:
    case report_path_full_clock:
    case report_path_full_clock_expanded:
      while (end_iter.hasNext()) {
	PathEnd *end = end_iter.next();
	string result;
	end->reportFull(this, result);
	report_->print(result);
	report_->print("\n\n");
      }
      break;
    case report_path_short:
      while (end_iter.hasNext()) {
	PathEnd *end = end_iter.next();
	string result;
	end->reportShort(this, result);
	report_->print(result);
	report_->print("\n\n");
      }
      break;
    case report_path_end:
      reportPathEndsEnd(ends);
      break;
    case report_path_summary:
      reportSummaryHeader(header);
      report_->print(header);
      while (end_iter.hasNext()) {
	PathEnd *end = end_iter.next();
	string result;
	reportSummaryLine(end, result);
	report_->print(result);
      }
      report_->print("\n");
      break;
    case report_path_slack_only:
      reportSlackOnlyHeader(header);
      report_->print(header);
      while (end_iter.hasNext()) {
	PathEnd *end = end_iter.next();
	string result;
	reportSlackOnly(end, result);
	report_->print(result);
      }
      report_->print("\n");
      break;
    default:
      internalError("unsupported path type");
      break;
    }
  }
}

void
ReportPath::reportPathEndsEnd(PathEndSeq *ends)
{
  PathGroup *prev_group = NULL;
  PathEndSeq::Iterator end_iter(ends);
  while (end_iter.hasNext()) {
    PathEnd *end = end_iter.next();
    PathGroup *group = search_->pathGroup(end);
    if (group != prev_group) {
      if (prev_group)
	report_->print("\n");
      const char *setup_hold = (end->minMax(this) == MinMax::min())
	? "min_delay/hold"
	: "max_delay/setup";
      report_->print("%s ('%s' group)\n\n",
		     setup_hold,
		     group->name());
      string header;
      reportEndHeader(header);
      report_->print(header);
    }
    string result;
    reportEndLine(end, result);
    report_->print(result);
    prev_group = group;
  }
  report_->print("\n");
}

void
ReportPath::reportPath(const Path *path)
{
  string result;
  reportPath(path, result);
  report_->print(result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportShort(const PathEndUnconstrained *end,
			string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
}

void
ReportPath::reportShort(const PathEndUnconstrained *end,
			PathExpanded &expanded,
			string &result)
{
  reportStartpoint(end, expanded, result);
  reportUnclockedEndpoint(end, "internal pin", result);
  reportGroup(end, result);
}

void
ReportPath::reportFull(const PathEndUnconstrained *end,
		       string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
  reportEndOfLine(result);

  reportPath(end, expanded, result);
  reportLine("data arrival time", end->dataArrivalTimeOffset(this), result);
  reportDashLine(result);
  result += "(Path is unconstrained)\n";
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportShort(const PathEndCheck *end,
			string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
}

void
ReportPath::reportShort(const PathEndCheck *end,
			PathExpanded &expanded,
			string &result)
{
  reportStartpoint(end, expanded, result);
  reportEndpoint(end, result);
  reportGroup(end, result);
}

void
ReportPath::reportFull(const PathEndCheck *end,
		       string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
  reportSrcPathArrival(end, expanded, result);
  reportTgtClk(end, result);
  reportRequired(end, checkRoleString(end), result);
  reportSlack(end, result);
}

char *
ReportPath::checkRoleString(const PathEnd *end)
{
  const char *check_role = end->checkRole(this)->asString();
  return stringPrintTmp(strlen("library  time")
			+ strlen(check_role) + 1,
			"library %s time",
			check_role);
}

void
ReportPath::reportEndpoint(const PathEndCheck *end,
			   string &result)
{
  Instance *inst = network_->instance(end->vertex(this)->pin());
  const char *inst_name = cmd_network_->pathName(inst);
  const char *clk_name = tgtClkName(end);
  const char *rise_fall = asRisingFalling(end->targetClkEndTrans(this));
  const TimingRole *check_role = end->checkRole(this);
  const TimingRole *check_generic_role = check_role->genericRole();
  if (check_role == TimingRole::recovery()
      || check_role == TimingRole::removal()) {
    const char *check_role_name = check_role->asString();
    char *reason = stringPrintTmp(strlen(" check against -edge clock ")
				  + strlen(check_role_name)
				  + strlen(rise_fall)
				  + strlen(clk_name) + 1,
				  "%s check against %s-edge clock %s",
				  check_role_name,
				  rise_fall,
				  clk_name);
    reportEndpoint(inst_name, reason, result);
  }
  else if (check_generic_role == TimingRole::setup()
	   || check_generic_role == TimingRole::hold()) {
    LibertyCell *cell = network_->libertyCell(inst);
    if (cell->isClockGate()) {
      const char *reason =
	stringPrintTmp(strlen( " clock gating-check end-point clocked by ")
		       + strlen(rise_fall)
		       + strlen(clk_name) + 1,
		       "%s clock gating-check end-point clocked by %s",
		       rise_fall, clk_name);
      reportEndpoint(inst_name, reason, result);
    }
    else {
      const char *reg_desc = clkRegLatchDesc(end);
      const char *reason = stringPrintTmp(strlen( " clocked by ")
					  + strlen(reg_desc)
					  + strlen(clk_name) + 1,
					  "%s clocked by %s",
					  reg_desc, clk_name);
      reportEndpoint(inst_name, reason, result);
    }
  }
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportShort(const PathEndLatchCheck *end,
			string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
}

void
ReportPath::reportShort(const PathEndLatchCheck *end,
			PathExpanded &expanded,
			string &result)
{
  reportStartpoint(end, expanded, result);
  reportEndpoint(end, result);
  reportGroup(end, result);
}

void
ReportPath::reportFull(const PathEndLatchCheck *end,
		       string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
  PathDelay *path_delay = end->pathDelay();
  reportEndOfLine(result);
  bool ignore_clk_latency = path_delay && path_delay->ignoreClkLatency();
  if (ignore_clk_latency) {
    // Based on reportSrcPath.
    reportPathHeader(result);
    reportPath3(end->path(), expanded, false, false, 0.0,
		end->sourceClkOffset(this), result);
  }
  else
    reportSrcPath(end, expanded, result);
  reportLine("data arrival time", end->dataArrivalTimeOffset(this), result);
  reportEndOfLine(result);

  Required req_time;
  Arrival borrow, adjusted_data_arrival, time_given_to_startpoint;
  end->latchRequired(this, req_time, borrow, adjusted_data_arrival,
		     time_given_to_startpoint);
  // Adjust required to requiredTimeOffset.
  req_time += end->sourceClkOffset(this);
  if (path_delay) {
    float delay = path_delay->delay();
    reportLine("max_delay", delay, delay, result);
    if (!ignore_clk_latency) {
      if (reportClkPath()
	  && isPropagated(end->targetClkPath()))
	reportTgtClk(end, delay, result);
      else
	reportCommonClkPessimism(end, delay, result);
    }
  }
  else
    reportTgtClk(end, result);

  if (borrow >= 0.0)
    reportLine("time borrowed from endpoint", borrow, req_time, result);
  else
    reportLine("time given to endpoint", borrow, req_time, result);
  reportLine("data required time", req_time, result);
  reportDashLine(result);
  reportSlack(end, result);
  if (end->checkGenericRole(this) == TimingRole::setup()
      && !ignore_clk_latency) {
    reportEndOfLine(result);
    reportBorrowing(end, borrow, time_given_to_startpoint, result);
  }
}

void
ReportPath::reportEndpoint(const PathEndLatchCheck *end,
			   string &result)
{
  Instance *inst = network_->instance(end->vertex(this)->pin());
  const char *inst_name = cmd_network_->pathName(inst);
  const char *clk_name = tgtClkName(end);
  const char *reg_desc = latchDesc(end);
  const char *reason = stringPrintTmp(strlen( " clocked by ")
				      + strlen(reg_desc)
				      + strlen(clk_name) + 1,
				      "%s clocked by %s",
				      reg_desc, clk_name);
  reportEndpoint(inst_name, reason, result);
}

const char *
ReportPath::latchDesc(const PathEndLatchCheck *end)
{
  // Liberty latch descriptions can have timing checks to the
  // wrong edge of the enable, so look up the EN->Q arcs and use
  // them to characterize the latch as positive/negative.
  TimingArc *check_arc = end->checkArc();
  TimingArcSet *check_set = check_arc->set();
  LibertyCell *cell = check_set->from()->libertyCell();
  TransRiseFall *enable_tr = cell->latchCheckEnableTrans(check_set);
  return latchDesc(enable_tr);
}

void
ReportPath::reportBorrowing(const PathEndLatchCheck *end,
			    Arrival &borrow,
			    Arrival &time_given_to_startpoint,
			    string &result)
{
  Delay open_latency, latency_diff, max_borrow;
  float nom_pulse_width, open_uncertainty, open_crpr, crpr_diff;
  bool borrow_limit_exists;
  end->latchBorrowInfo(this, nom_pulse_width, open_latency, latency_diff,
		       open_uncertainty, open_crpr, crpr_diff,
		       max_borrow, borrow_limit_exists);
  result += "Time Borrowing Information\n";
  reportDashLineTotal(result);
  if (borrow_limit_exists)
    reportLineTotal("user max time borrow", max_borrow, result);
  else {
    const char *tgt_clk_name = tgtClkName(end);
    Arrival tgt_clk_width = end->targetClkWidth(this);
    const Path *tgt_clk_path = end->targetClkPath();
    if (tgt_clk_path->clkInfo(search_)->isPropagated()) {
      const char *width_msg = stringPrintTmp(strlen(" nominal pulse width")
					     + strlen(tgt_clk_name) + 1,
					     "%s nominal pulse width",
					     tgt_clk_name);
      reportLineTotal(width_msg, nom_pulse_width, result);
      if (!delayFuzzyZero(latency_diff))
	reportLineTotalMinus("clock latency difference", latency_diff, result);
    }
    else {
      const char *width_msg = stringPrintTmp(strlen(" pulse width")
					     + strlen(tgt_clk_name) + 1,
					     "%s pulse width",
					     tgt_clk_name);
      reportLineTotal(width_msg, tgt_clk_width, result);
    }
    ArcDelay margin = end->margin(this);
    reportLineTotalMinus("library setup time", margin, result);
    reportDashLineTotal(result);
    if (!fuzzyZero(crpr_diff))
      reportLineTotalMinus("CRPR difference", crpr_diff, result);
    reportLineTotal("max time borrow", max_borrow, result);
  }
  if (delayFuzzyGreater(borrow, delay_zero)
      && (!fuzzyZero(open_uncertainty)
	  || !fuzzyZero(open_crpr))) {
    reportDashLineTotal(result);
    reportLineTotal("actual time borrow", borrow, result);
    if (!fuzzyZero(open_uncertainty))
      reportLineTotal("open edge uncertainty", open_uncertainty, result);
    if (!fuzzyZero(open_crpr))
      reportLineTotal("open edge CRPR", open_crpr, result);
    reportDashLineTotal(result);
    reportLineTotal("time given to startpoint", time_given_to_startpoint,
		    result);
  }
  else
    reportLineTotal("actual time borrow", borrow, result);
  reportDashLineTotal(result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportShort(const PathEndPathDelay *end,
			string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
}

void
ReportPath::reportShort(const PathEndPathDelay *end,
			PathExpanded &expanded,
			string &result)
{
  reportStartpoint(end, expanded, result);
  if (end->targetClk(this))
    reportEndpoint(end, result);
  else
    reportUnclockedEndpoint(end, "internal path endpoint", result);
  reportGroup(end, result);
}

void
ReportPath::reportEndpoint(const PathEndPathDelay *end,
			   string &result)
{
  Instance *inst = network_->instance(end->vertex(this)->pin());
  const char *inst_name = cmd_network_->pathName(inst);
  const char *clk_name = tgtClkName(end);
  const char *reg_desc = clkRegLatchDesc(end);
  const char *reason = stringPrintTmp(strlen(" clocked by ")
				      + strlen(reg_desc)
				      + strlen(clk_name) + 1,
				      "%s clocked by %s",
				      reg_desc,
				      clk_name);
  reportEndpoint(inst_name, reason, result);
}

void
ReportPath::reportFull(const PathEndPathDelay *end,
		       string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);

  // Based on reportSrcPathArrival.
  reportEndOfLine(result);
  PathDelay *path_delay = end->pathDelay();
  if (path_delay->ignoreClkLatency()) {
    // Based on reportSrcPath.
    reportPathHeader(result);
    reportPath3(end->path(), expanded, false, false, 0.0,
		end->sourceClkOffset(this), result);
  }
  else
    reportSrcPath(end, expanded, result);
  reportLine("data arrival time", end->dataArrivalTimeOffset(this), result);
  reportEndOfLine(result);

  ArcDelay margin = end->margin(this);
  MinMax *min_max = path_delay->minMax()->asMinMax();
  if (min_max == MinMax::max())
    margin = -margin;

  const char *margin_what;
  if (end->pathDelayMarginIsExternal())
    margin_what = "output external delay";
  else
    margin_what = checkRoleString(end);
  const char *min_max_str = min_max->asString();
  char *delay_msg = stringPrintTmp(strlen(" delay")
				   + strlen(min_max_str) + 1,
				   "%s_delay",
				   min_max_str);
  float delay = path_delay->delay();
  reportLine(delay_msg, delay, delay, result);
  if (!path_delay->ignoreClkLatency()) {
    const Path *tgt_clk_path = end->targetClkPath();
    if (tgt_clk_path) {
      float delay = 0.0;
      if (path_delay)
	delay = path_delay->delay();
      if (reportClkPath()
	  && isPropagated(tgt_clk_path))
	reportTgtClk(end, delay, result);
      else {
	Arrival tgt_clk_delay = end->targetClkDelay(this);
	Arrival tgt_clk_arrival = delay + tgt_clk_delay;
	if (!delayFuzzyZero(tgt_clk_delay))
	  reportLine(clkNetworkDelayIdealProp(isPropagated(tgt_clk_path)),
		     tgt_clk_delay, tgt_clk_arrival, result);
	reportClkUncertainty(end, tgt_clk_arrival, result);
	reportCommonClkPessimism(end, tgt_clk_arrival, result);
      }
    }
  }
  reportRequired(end, margin_what, result);
  reportSlack(end, result);
}

bool
ReportPath::isPropagated(const Path *clk_path)
{
  return clk_path->clkInfo(search_)->isPropagated();
}

bool
ReportPath::isPropagated(const Path *clk_path,
			 Clock *clk)
{
  if (clk_path)
    return clk_path->clkInfo(search_)->isPropagated();
  else
    return clk->isPropagated();
}

const char *
ReportPath::clkNetworkDelayIdealProp(bool is_prop)
{
  if (is_prop)
    return "clock network delay (propagated)";
  else
    return "clock network delay (ideal)";
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportShort(const PathEndOutputDelay *end,
			string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
}

void
ReportPath::reportShort(const PathEndOutputDelay *end,
			PathExpanded &expanded,
			string &result)
{
  reportStartpoint(end, expanded, result);
  reportEndpoint(end, result);
  reportGroup(end, result);
}

void
ReportPath::reportFull(const PathEndOutputDelay *end,
		       string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
  reportSrcPathArrival(end, expanded, result);
  reportTgtClk(end, result);
  reportRequired(end, "output external delay", result);
  reportSlack(end, result);
}

void
ReportPath::reportEndpoint(const PathEndOutputDelay *end,
			   string &result)
{
  Vertex *vertex = end->vertex(this);
  Pin *pin = vertex->pin();
  const char *pin_name = cmd_network_->pathName(pin);
  Clock *tgt_clk = end->targetClk(this);
  if (network_->isTopLevelPort(pin)) {
    // Pin direction is "output" even for bidirects.
    if (tgt_clk) {
      const char *clk_name = tgtClkName(end);
      const char *reason = stringPrintTmp(strlen("output port clocked by ")
					  + strlen(clk_name) + 1,
					  "output port clocked by %s",
					  clk_name);
      reportEndpoint(pin_name, reason, result);
    }
    else
      reportEndpoint(pin_name, "output port", result);
  }
  else {
    if (tgt_clk) {
      const char *clk_name = tgtClkName(end);
      const char *reason = stringPrintTmp(strlen("internal path endpoint clocked by ")
					  + strlen(clk_name) + 1,
					  "internal path endpoint clocked by %s",
					  clk_name);
      reportEndpoint(pin_name, reason, result);
    }
    else
      reportEndpoint(pin_name, "internal path endpoint", result);
  }
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportShort(const PathEndGatedClock *end,
			string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
}

void
ReportPath::reportShort(const PathEndGatedClock *end,
			PathExpanded &expanded,
			string &result)
{
  reportStartpoint(end, expanded, result);
  reportEndpoint(end, result);
  reportGroup(end, result);
}

void
ReportPath::reportFull(const PathEndGatedClock *end,
		       string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
  reportSrcPathArrival(end, expanded, result);
  reportTgtClk(end, result);
  reportRequired(end, checkRoleReason(end), result);
  reportSlack(end, result);
}

void
ReportPath::reportEndpoint(const PathEndGatedClock *end,
			   string &result)
{
  Instance *inst = network_->instance(end->vertex(this)->pin());
  const char *inst_name = cmd_network_->pathName(inst);
  const char *clk_name = tgtClkName(end);
  const TransRiseFall *clk_end_tr = end->targetClkEndTrans(this);
  const TransRiseFall *clk_tr =
    (end->minMax(this) == MinMax::max()) ? clk_end_tr : clk_end_tr->opposite();
  const char *rise_fall = asRisingFalling(clk_tr);
  // Note that target clock transition is ignored.
  const char *reason = stringPrintTmp(strlen(" clock gating-check end-point clocked by ")
				      + strlen(rise_fall)
				      + strlen(clk_name) + 1,
				      "%s clock gating-check end-point clocked by %s",
				      rise_fall,
				      clk_name);
  reportEndpoint(inst_name, reason, result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportShort(const PathEndDataCheck *end,
			string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
}

void
ReportPath::reportShort(const PathEndDataCheck *end,
			PathExpanded &expanded,
			string &result)
{
  reportStartpoint(end, expanded, result);
  reportEndpoint(end, result);
  reportGroup(end, result);

}

void
ReportPath::reportFull(const PathEndDataCheck *end,
		       string &result)
{
  PathExpanded expanded(end->path(), this);
  reportShort(end, expanded, result);
  reportSrcPathArrival(end, expanded, result);

  // Capture/target clock path reporting resembles both source (reportSrcPath)
  // and target (reportTgtClk) clocks. 
  // It is like a source because it can be a non-clock path.
  // It is like a target because crpr and uncertainty are reported.
  // It is always propagated, even if the clock is ideal.
  reportTgtClk(end, 0.0, true, result);
  const PathVertex *data_clk_path = end->dataClkPath();
  if (!data_clk_path->isClock(this)) {
    // Report the path from the clk network to the data check.
    PathExpanded clk_expanded(data_clk_path, this);
    float src_offset = end->sourceClkOffset(this);
    float clk_delay = end->targetClkDelay(this);
    float clk_arrival = end->targetClkArrival(this);
    float offset = clk_arrival - clk_delay + src_offset;
    reportPath5(data_clk_path, clk_expanded, clk_expanded.startIndex(),
		clk_expanded.size() - 1,
		data_clk_path->clkInfo(search_)->isPropagated(), false,
		// Delay to startpoint is already included.
		clk_arrival + src_offset, offset, result);
  }
  reportRequired(end, checkRoleReason(end), result);
  reportSlack(end, result);
}

void
ReportPath::reportEndpoint(const PathEndDataCheck *end,
			   string &result)
{
  Instance *inst = network_->instance(end->vertex(this)->pin());
  const char *inst_name = cmd_network_->pathName(inst);
  const char *tgt_clk_tr = asRisingFalling(end->dataClkPath()->transition(this));
  const char *tgt_clk_name = end->targetClk(this)->name();
  char *reason = stringPrintTmp(strlen(" edge-triggered data to data check clocked by ")
				+ strlen(tgt_clk_tr)
				+ strlen(tgt_clk_name) + 1,
				"%s edge-triggered data to data check clocked by %s",
				tgt_clk_tr,
				tgt_clk_name);

  reportEndpoint(inst_name, reason, result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportEndHeader(string &result)
{
  // Line one.
  reportDescription("", result);
  result += ' ';
  reportField("Required ", field_total_, result);
  result += ' ';
  reportField("Actual  ", field_total_, result);
  reportEndOfLine(result);

  // Line two.
  reportDescription("Endpoint", result);
  result += ' ';
  reportField("Path Delay", field_total_, result);
  result += ' ';
  reportField("Path Delay", field_total_, result);
  result += ' ';
  reportField("Slack", field_total_, result);
  reportEndOfLine(result);

  reportDashLine(field_description_->width() + field_total_->width() * 3 + 3,
		 result);
}

void
ReportPath::reportEndLine(PathEnd *end,
			  string &result)
{
  reportDescription(pathEndpoint(end), result);
  reportSpaceFieldTime(end->requiredTimeOffset(this), result);
  reportSpaceFieldTime(end->dataArrivalTimeOffset(this), result);
  Slack slack = end->slack(this);
  reportSpaceFieldTime(slack, result);
  result += (slack >= 0.0) ? " (MET)" : " (VIOLATED)";
  reportEndOfLine(result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportSummaryHeader(string &result)
{
  reportDescription("Startpoint", result);
  reportDescription("Endpoint", result);
  result += ' ';
  reportField("Slack", field_total_, result);
  reportEndOfLine(result);
  reportDashLine(field_description_->width() * 2 + field_total_->width() + 1,
		 result);
  reportEndOfLine(result);
}

void
ReportPath::reportSummaryLine(PathEnd *end,
			      string &result)
{
  PathExpanded expanded(end->path(), this);
  reportDescription(pathStartpoint(end, expanded), result);
  reportDescription(pathEndpoint(end), result);
  if (end->isUnconstrained())
    reportSpaceFieldTime(end->dataArrivalTimeOffset(this), result);
  else
    reportSpaceFieldTime(end->slack(this), result);
  reportEndOfLine(result);
}

const char *
ReportPath::pathStartpoint(PathEnd *end,
			   PathExpanded &expanded)
{
  PathRef *start = expanded.startPath();
  Pin *pin = start->pin(graph_);
  const char *pin_name = cmd_network_->pathName(pin);
  if (network_->isTopLevelPort(pin)) {
    PortDirection *dir = network_->direction(pin);
    const char *dir_str = dir->name();
    return stringPrintTmp(strlen(pin_name) + strlen(dir_str) + 4,
			  "%s (%s)",
			  pin_name, dir_str);
  }
  else {
    Instance *inst = network_->instance(end->vertex(this)->pin());
    const char *cell_name = cmd_network_->name(network_->cell(inst));
    return stringPrintTmp(strlen(pin_name) + strlen(cell_name) + 4,
			  "%s (%s)",
			  pin_name,
			  cell_name);
  }
}

const char *
ReportPath::pathEndpoint(PathEnd *end)
{
  Pin *pin = end->vertex(this)->pin();
  const char *pin_name = cmd_network_->pathName(pin);
  if (network_->isTopLevelPort(pin)) {
    PortDirection *dir = network_->direction(pin);
    const char *dir_str = dir->name();
    return stringPrintTmp(strlen(pin_name) + strlen(dir_str) + 4,
			  "%s (%s)",
			  pin_name, dir_str);
  }
  else {
    Instance *inst = network_->instance(end->vertex(this)->pin());
    const char *cell_name = cmd_network_->name(network_->cell(inst));
    return stringPrintTmp(strlen(pin_name) + strlen(cell_name) + 4,
			  "%s (%s)",
			  pin_name,
			  cell_name);
  }
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportSlackOnlyHeader(string &result)
{
  reportDescription("Group", result);
  result += ' ';
  reportField("Slack", field_total_, result);
  reportEndOfLine(result);
  reportDashLine(field_description_->width() + field_total_->width() + 1,
		 result);
}

void
ReportPath::reportSlackOnly(PathEnd *end,
			    string &result)
{
  reportDescription(search_->pathGroup(end)->name(), result);
  if (end->isUnconstrained())
    reportSpaceFieldTime(end->dataArrivalTimeOffset(this), result);
  else
    reportSpaceFieldTime(end->slack(this), result);
  reportEndOfLine(result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportMpwCheck(MinPulseWidthCheck *check,
			   bool verbose)
{
  if (verbose) {
    string result;
    reportVerbose(check, result);
    report_->print(result);
    report_->print("\n");
  }
  else {
    string header;
    reportMpwHeaderShort(header);
    report_->print(header);
    string result;
    reportShort(check, result);
    report_->print(result);
  }
  report_->print("\n");
}

void
ReportPath::reportMpwChecks(MinPulseWidthCheckSeq *checks,
			    bool verbose)
{
  if (!checks->empty()) {
    if (verbose) {
      MinPulseWidthCheckSeq::Iterator check_iter(checks);
      while (check_iter.hasNext()) {
	MinPulseWidthCheck *check = check_iter.next();
	string result;
	reportVerbose(check, result);
	report_->print(result);
	report_->print("\n");
      }
    }
    else {
      string header;
      reportMpwHeaderShort(header);
      report_->print(header);
      MinPulseWidthCheckSeq::Iterator check_iter(checks);
      while (check_iter.hasNext()) {
	MinPulseWidthCheck *check = check_iter.next();
	string result;
	reportShort(check, result);
	report_->print(result);
      }
    }
    report_->print("\n");
  }
}

void
ReportPath::reportMpwHeaderShort(string &result)
{
  reportDescription("", result);
  result += ' ';
  reportField("Required", field_total_, result);
  result += ' ';
  reportField("Actual", field_total_, result);
  reportEndOfLine(result);

  reportDescription("Pin", result);
  result += ' ';
  reportField("width ", field_total_, result);
  result += ' ';
  reportField("width", field_total_, result);
  result += ' ';
  reportField("Slack", field_total_, result);
  reportEndOfLine(result);
  reportDashLine(field_description_->width() + field_total_->width() * 3 + 3,
		 result);
}

void
ReportPath::reportShort(MinPulseWidthCheck *check,
			string &result)
{
  const char *pin_name = cmd_network_->pathName(check->pin(this));
  const char *hi_low = mpwCheckHiLow(check);
  char *what = stringPrintTmp(strlen(" ()")
			      + strlen(pin_name)
			      + strlen(hi_low) + 1,
			      "%s (%s)",
			      pin_name,
			      hi_low);
  reportDescription(what, result);
  reportSpaceFieldTime(check->minWidth(this), result);
  reportSpaceFieldTime(check->width(this), result);
  Slack slack = check->slack(this);
  reportSpaceFieldTime(slack, result);
  result += (slack >= 0.0) ? " (MET)" : " (VIOLATED)";
  reportEndOfLine(result);
}

void
ReportPath::reportVerbose(MinPulseWidthCheck *check,
			  string &result)
{
  const char *pin_name = cmd_network_->pathName(check->pin(this));
  result += "Pin: ";
  result += pin_name;
  reportEndOfLine(result);

  result += "Check: sequential_clock_pulse_width\n";
  reportEndOfLine(result);

  reportPathHeader(result);
  ClockEdge *open_clk_edge = check->openClkEdge(this);
  Clock *open_clk = open_clk_edge->clock();
  const char *open_clk_name = open_clk->name();
  const char *open_rise_fall = asRiseFall(open_clk_edge->transition());
  float open_clk_time = open_clk_edge->time();
  const char *open_clk_msg = stringPrintTmp(strlen("clock  ( edge)")
					    + strlen(open_clk_name)
					    + strlen(open_rise_fall) + 1,
					    "clock %s (%s edge)",
					    open_clk_name,
					    open_rise_fall);
  reportLine(open_clk_msg, open_clk_time, open_clk_time, result);
  Arrival open_arrival = check->openArrival(this);
  bool is_prop = isPropagated(check->openPath());
  const char *clk_ideal_prop = clkNetworkDelayIdealProp(is_prop);
  reportLine(clk_ideal_prop, check->openDelay(this), open_arrival, result);
  reportLine(pin_name, delay_zero, open_arrival, result);
  reportLine("open edge arrival time", open_arrival, result);
  reportEndOfLine(result);

  ClockEdge *close_clk_edge = check->closeClkEdge(this);
  Clock *close_clk = close_clk_edge->clock();
  const char *close_clk_name = close_clk->name();
  const char *close_rise_fall = asRiseFall(close_clk_edge->transition());
  float close_offset = check->closeOffset(this);
  float close_clk_time = close_clk_edge->time() + close_offset;
  const char *close_clk_msg = stringPrintTmp(strlen("clock  ( edge)")
					     + strlen(close_clk_name)
					     + strlen(close_rise_fall) + 1,
					     "clock %s (%s edge)",
					     close_clk_name,
					     close_rise_fall);
  reportLine(close_clk_msg, close_clk_time, close_clk_time, result);
  Arrival close_arrival = check->closeArrival(this) + close_offset;
  reportLine(clk_ideal_prop, check->closeDelay(this), close_arrival, result);
  reportLine(pin_name, delay_zero, close_arrival, result);

  if (sdc_->crprEnabled()) {
    float pessimism = check->commonClkPessimism(this);
    close_arrival += pessimism;
    reportLine("clock reconvergence pessimism", pessimism, close_arrival,
	       result);
  }
  reportLine("close edge arrival time", close_arrival, result);
  reportDashLine(result);
  float min_width = check->minWidth(this);
  const char *hi_low = mpwCheckHiLow(check);
  const char *rpw_msg = stringPrintTmp(strlen("required pulse width ()")
				       + strlen(hi_low) + 1,
				       "required pulse width (%s)",
				       hi_low);
  reportLine(rpw_msg, min_width, result);
  reportLine("actual pulse width", check->width(this), result);
  reportDashLine(result);
  reportSlack(check->slack(this), result);
}

const char *
ReportPath::mpwCheckHiLow(MinPulseWidthCheck *check)
{
  if (check->openTransition(this) == TransRiseFall::rise())
    return "high";
  else
    return "low";
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportCheck(MinPeriodCheck *check,
			bool verbose)
{
  if (verbose) {
    string result;
    reportVerbose(check, result);
    report_->print(result);
    report_->print("\n");
  }
  else {
    string header;
    reportPeriodHeaderShort(header);
    report_->print(header);
    string result;
    reportShort(check, result);
    report_->print(result);
  }
  report_->print("\n");
}

void
ReportPath::reportChecks(MinPeriodCheckSeq *checks,
			 bool verbose)
{
  if (!checks->empty()) {
    if (verbose) {
      MinPeriodCheckSeq::Iterator check_iter(checks);
      while (check_iter.hasNext()) {
	MinPeriodCheck *check = check_iter.next();
	string result;
	reportVerbose(check, result);
	report_->print(result);
	report_->print("\n");
      }
    }
    else {
      string header;
      reportPeriodHeaderShort(header);
      report_->print(header);
      MinPeriodCheckSeq::Iterator check_iter(checks);
      while (check_iter.hasNext()) {
	MinPeriodCheck *check = check_iter.next();
	string result;
	reportShort(check, result);
	report_->print(result);
      }
    }
    report_->print("\n");
  }
}

void
ReportPath::reportPeriodHeaderShort(string &result)
{
  reportDescription("", result);
  result += ' ';
  reportField("", field_total_, result);
  result += ' ';
  reportField("Min ", field_total_, result);
  result += ' ';
  reportField("", field_total_, result);
  reportEndOfLine(result);

  reportDescription("Pin", result);
  result += ' ';
  reportField("Period", field_total_, result);
  result += ' ';
  reportField("Period", field_total_, result);
  result += ' ';
  reportField("Slack", field_total_, result);
  reportEndOfLine(result);
  reportDashLine(field_description_->width() + field_total_->width() * 3 + 3,
		 result);
}

void
ReportPath::reportShort(MinPeriodCheck *check,
			string &result)
{
  const char *pin_name = cmd_network_->pathName(check->pin());
  reportDescription(pin_name, result);
  reportSpaceFieldTime(check->period(), result);
  reportSpaceFieldTime(check->minPeriod(this), result);
  Slack slack = check->slack(this);
  reportSpaceFieldTime(slack, result);
  result += (slack >= 0.0) ? " (MET)" : " (VIOLATED)";
  reportEndOfLine(result);
}

void
ReportPath::reportVerbose(MinPeriodCheck *check, string &result)
{
  const char *pin_name = cmd_network_->pathName(check->pin());
  result += "Pin: ";
  result += pin_name;
  reportEndOfLine(result);

  reportLine("Period", check->period(), result);
  reportLine("min_period", -check->minPeriod(this), result);
  reportDashLine(result);
  reportSlack(check->slack(this), result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportCheck(MaxSkewCheck *check,
			bool verbose)
{
  if (verbose) {
    string result;
    reportVerbose(check, result);
    report_->print(result);
    report_->print("\n");
  }
  else {
    string header;
    reportMaxSkewHeaderShort(header);
    report_->print(header);
    string result;
    reportShort(check, result);
    report_->print(result);
  }
  report_->print("\n");
}

void
ReportPath::reportChecks(MaxSkewCheckSeq *checks,
			 bool verbose)
{
  if (!checks->empty()) {
    if (verbose) {
      MaxSkewCheckSeq::Iterator check_iter(checks);
      while (check_iter.hasNext()) {
	MaxSkewCheck *check = check_iter.next();
	string result;
	reportVerbose(check, result);
	report_->print(result);
	report_->print("\n");
      }
    }
    else {
      string header;
      reportMaxSkewHeaderShort(header);
      report_->print(header);
      MaxSkewCheckSeq::Iterator check_iter(checks);
      while (check_iter.hasNext()) {
	MaxSkewCheck *check = check_iter.next();
	string result;
	reportShort(check, result);
	report_->print(result);
      }
    }
    report_->print("\n");
  }
}

void
ReportPath::reportMaxSkewHeaderShort(string &result)
{
  reportDescription("", result);
  result += ' ';
  reportField("Required", field_total_, result);
  result += ' ';
  reportField("Actual", field_total_, result);
  result += ' ';
  reportField("", field_total_, result);
  reportEndOfLine(result);

  reportDescription("Pin", result);
  result += ' ';
  reportField("Skew", field_total_, result);
  result += ' ';
  reportField("Skew", field_total_, result);
  result += ' ';
  reportField("Slack", field_total_, result);
  reportEndOfLine(result);
  reportDashLine(field_description_->width() + field_total_->width() * 3 + 3,
		 result);
}

void
ReportPath::reportShort(MaxSkewCheck *check,
			string &result)
{
  Pin *clk_pin = check->clkPin(this);
  const char *clk_pin_name = network_->pathName(clk_pin);
  TimingArc *check_arc = check->checkArc();
  const char *what = stringPrintTmp(strlen(clk_pin_name)
				    + strlen(" (r->r)") + 1,
				    "%s (%s->%s)",
				    clk_pin_name,
				    check_arc->fromTrans()->asString(),
				    check_arc->toTrans()->asString());
  reportDescription(what, result);
  reportSpaceFieldTime(check->maxSkew(this), result);
  reportSpaceFieldTime(check->skew(this), result);
  Slack slack = check->slack(this);
  reportSpaceFieldTime(slack, result);
  result += (slack >= 0.0) ? " (MET)" : " (VIOLATED)";
  reportEndOfLine(result);
}

void
ReportPath::reportVerbose(MaxSkewCheck *check,
			  string &result)
{
  const char *clk_pin_name = cmd_network_->pathName(check->clkPin(this));
  result += "Constrained Pin: ";
  result += clk_pin_name;
  reportEndOfLine(result);

  const char *ref_pin_name = cmd_network_->pathName(check->refPin(this));
  result += "Reference   Pin: ";
  result += ref_pin_name;
  reportEndOfLine(result);

  result += "Check: max_skew";
  reportEndOfLine(result);
  reportEndOfLine(result);

  reportPathHeader(result);
  reportSkewClkPath("reference pin arrival time", check->refPath(), result);
  reportSkewClkPath("constrained pin arrival time", check->clkPath(), result);

  reportDashLine(result);
  reportLine("allowable skew", check->maxSkew(this), result);
  reportLine("actual skew", check->skew(this), result);
  reportDashLine(result);
  reportSlack(check->slack(this), result);
}

// Based on reportTgtClk.
void
ReportPath::reportSkewClkPath(const char *arrival_msg,
			      const PathVertex *clk_path,
			      string &result)
{
  ClockEdge *clk_edge = clk_path->clkEdge(this);
  Clock *clk = clk_edge->clock();
  const TransRiseFall *clk_tr = clk_edge->transition();
  const TransRiseFall *clk_end_tr = clk_path->transition(this);
  const char *clk_name = (clk_end_tr == clk_tr)
    ? clk->name()
    : clkNameInverted(clk->name());
  float clk_time = clk_edge->time();
  const Arrival &clk_arrival = search_->clkPathArrival(clk_path);
  Arrival clk_delay = clk_arrival - clk_time;
  PathAnalysisPt *path_ap = clk_path->pathAnalysisPt(this);
  const MinMax *min_max = path_ap->pathMinMax();
  Vertex *clk_vertex = clk_path->vertex(this);
  reportClkLine(clk, clk_name, clk_end_tr, clk_time, min_max, result);
  bool is_prop = isPropagated(clk_path);
  if (is_prop && reportClkPath()) {
    const EarlyLate *early_late = TimingRole::skew()->tgtClkEarlyLate();
    if (reportGenClkSrcPath(clk_path, clk, clk_tr, min_max, early_late))
      reportGenClkSrcAndPath(clk_path, clk, clk_tr, early_late, path_ap,
			     0.0, 0.0, false, result);
    else {
      Arrival insertion, latency;
      PathEnd::checkTgtClkDelay(clk_path, clk_edge, TimingRole::skew(), this,
				insertion, latency);
      reportClkSrcLatency(insertion, clk_time, result);
      PathExpanded clk_expanded(clk_path, this);
      reportPath2(clk_path, clk_expanded, false, 0.0, result);
    }
  }
  else {
    reportLine(clkNetworkDelayIdealProp(is_prop), clk_delay, clk_arrival,
	       result);
    reportLine(descriptionField(clk_vertex), clk_arrival, clk_end_tr, result);
  }
  reportLine(arrival_msg, search_->clkPathArrival(clk_path), result);
  reportEndOfLine(result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportSlewLimitShortHeader()
{
  string result;
  reportSlewLimitShortHeader(result);
  report_->print(result);
}

void
ReportPath::reportSlewLimitShortHeader(string &result)
{
  reportDescription("Pin", result);
  result += ' ';
  reportField("Limit", field_slew_, result);
  result += ' ';
  reportField("Trans", field_slew_, result);
  result += ' ';
  reportField("Slack", field_slew_, result);
  reportEndOfLine(result);
  reportDashLine(field_description_->width() + field_slew_->width() * 3 + 3,
		 result);
}

void
ReportPath::reportSlewLimitShort(Pin *pin,
				 const TransRiseFall *tr,
				 Slew slew,
				 float limit,
				 float slack)
{
  string result;
  reportSlewLimitShort(pin, tr, slew, limit, slack, result);
  report_->print(result);
}

void
ReportPath::reportSlewLimitShort(Pin *pin,
				 const TransRiseFall *,
				 Slew slew,
				 float limit,
				 float slack,
				 string &result)
{
  const char *pin_name = cmd_network_->pathName(pin);
  reportDescription(pin_name, result);
  reportSpaceFieldTime(limit, result);
  reportSpaceFieldTime(slew, result);
  reportSpaceFieldTime(slack, result);
  result += (slack >= 0.0) ? " (MET)" : " (VIOLATED)";
  reportEndOfLine(result);
}

void
ReportPath::reportSlewLimitVerbose(Pin *pin,
				   const Corner *corner,
				   const TransRiseFall *tr,
				   Slew slew,
				   float limit,
				   float slack,
				   const MinMax *min_max)
{
  string result;
  reportSlewLimitVerbose(pin, corner, tr, slew, limit, slack, min_max, result);
  report_->print(result);
}

void
ReportPath::reportSlewLimitVerbose(Pin *pin,
				   const Corner *,
				   const TransRiseFall *tr,
				   Slew slew,
				   float limit,
				   float slack,
				   const MinMax *min_max,
				   string &result)
{
  result += "Pin ";
  result += cmd_network_->pathName(pin);
  result += ' ';
  result += tr->shortName();
  reportEndOfLine(result);

  result += min_max->asString();
  result += "_transition ";
  reportSpaceFieldTime(limit, result);
  reportEndOfLine(result);

  result += "transition_time ";
  reportField(delayAsFloat(slew), field_slew_, result);
  reportEndOfLine(result);

  reportDashLine(strlen("transition_time") + field_slew_->width() + 1,
		 result);

  result += "Slack          ";
  reportSpaceFieldTime(slack, result);
  result += (slack >= 0.0) ? " (MET)" : " (VIOLATED)";
  reportEndOfLine(result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportStartpoint(const PathEnd *end,
			     PathExpanded &expanded,
			     string &result)
{
  const Path *path = end->path();
  PathRef *start = expanded.startPath();
  TimingArc *prev_arc = expanded.startPrevArc();
  Edge *prev_edge = start->prevEdge(prev_arc, this);
  Pin *pin = start->pin(graph_);
  ClockEdge *clk_edge = path->clkEdge(this);
  Clock *clk = path->clock(search_);
  const char *pin_name = cmd_network_->pathName(pin);
  if (pathFromClkPin(path, pin)) {
    const char *clk_name = clk->name();
    const char *reason = stringPrintTmp(strlen("clock source ''")
					+ strlen(clk_name) + 1,
					"clock source '%s'", clk_name);
    reportStartpoint(pin_name, reason, result);
  }
  else if (network_->isTopLevelPort(pin)) {
    if (clk
	&& clk != sdc_->defaultArrivalClock()) {
      const char *clk_name = clk->name();
      // Pin direction is "input" even for bidirects.
      const char *reason=stringPrintTmp(strlen("input port clocked by ")
					+ strlen(clk_name) + 1,
					"input port clocked by %s",
					clk_name);
      reportStartpoint(pin_name, reason, result);
    }
    else
      reportStartpoint(pin_name, "input port", result);
  }
  else if (network_->isLeaf(pin) && prev_arc) {
    Instance *inst = network_->instance(pin);
    const char *inst_name = cmd_network_->pathName(inst);
    if (clk_edge) {
      const char *clk_name = clk->name();
      const TransRiseFall *clk_tr = clk_edge->transition();
      const TransRiseFall *clk_end_tr = clk_tr;
      PathRef clk_path;
      expanded.clkPath(clk_path);
      if (!clk_path.isNull()) {
	clk_end_tr = clk_path.transition(this);
	if (clk_end_tr != clk_tr)
	  clk_name = clkNameInverted(clk_name);
      }
      const char *reg_desc = edgeRegLatchDesc(prev_edge, prev_arc);
      const char *reason = stringPrintTmp(strlen(" clocked by ")
					  + strlen(reg_desc)
					  + strlen(clk_name) + 1,
					  "%s clocked by %s",
					  reg_desc, clk_name);
      reportStartpoint(inst_name, reason, result);
    }
    else {
      const char *reg_desc = edgeRegLatchDesc(prev_edge, prev_arc);
      reportStartpoint(inst_name, reg_desc, result);
    }
  }
  else if (network_->isLeaf(pin)) {
    if (clk_edge) {
      Clock *clk = clk_edge->clock();
      if (clk != sdc_->defaultArrivalClock()) {
	const char *clk_name = clk->name();
	const char *reason =
	  stringPrintTmp(strlen("internal path startpoint clocked by ")
			 + strlen(clk_name) + 1,
			 "internal path startpoint clocked by %s",
			 clk_name);
	reportStartpoint(pin_name, reason, result);
      }
      else
	reportStartpoint(pin_name, "internal path startpoint", result);
    }
    else
      reportStartpoint(pin_name, "internal pin", result);
  }
  else
    reportStartpoint(pin_name, "", result);
}

bool
ReportPath::pathFromClkPin(PathExpanded &expanded)
{
  PathRef *start = expanded.startPath();
  PathRef *end = expanded.endPath();
  const Pin *start_pin = start->pin(graph_);
  return pathFromClkPin(end, start_pin);
}

bool
ReportPath::pathFromClkPin(const Path *path,
			   const Pin *start_pin)
{
  Clock *clk = path->clock(search_);
  return clk
    && clk->vertexPins()
    && clk->vertexPins()->hasKey(const_cast<Pin*>(start_pin));
}

void
ReportPath::reportStartpoint(const char *start,
			     const char *reason,
			     string &result)
{
  reportStartEndPoint(start, reason, "Startpoint", result);
}

void
ReportPath::reportUnclockedEndpoint(const PathEnd *end,
				    const char *default_reason,
				    string &result)
{
  Vertex *vertex = end->vertex(this);
  Pin *pin = vertex->pin();
  if (network_->isTopLevelPort(pin)) {
    // Pin direction is "output" even for bidirects.
    reportEndpoint(cmd_network_->pathName(pin), "output port", result);
  }
  else if (network_->isLeaf(pin)) {
    VertexInEdgeIterator edge_iter(vertex, graph_);
    while (edge_iter.hasNext()) {
      Edge *edge = edge_iter.next();
      if (edge->role()->genericRole() == TimingRole::setup()) {
	Vertex *clk_vertex = edge->from(graph_);
	VertexOutEdgeIterator clk_edge_iter(clk_vertex, graph_);
	while (clk_edge_iter.hasNext()) {
	  Edge *clk_edge = clk_edge_iter.next();
	  if (clk_edge->role() == TimingRole::regClkToQ()) {
	    Instance *inst = network_->instance(pin);
	    const char *inst_name = cmd_network_->pathName(inst);
	    const char *reason = regDesc(clk_edge->timingArcSet()->isRisingFallingEdge());
	    reportEndpoint(inst_name, reason, result);
	    return;
	  }
	  if (clk_edge->role() == TimingRole::latchEnToQ()) {
	    Instance *inst = network_->instance(pin);
	    const char *inst_name = cmd_network_->pathName(inst);
	    const char *reason = latchDesc(clk_edge->timingArcSet()->isRisingFallingEdge());
	    reportEndpoint(inst_name, reason, result);
	    return;
	  }
	}
      }
    }
    reportEndpoint(cmd_network_->pathName(pin), default_reason, result);
  }
  else
    reportEndpoint(cmd_network_->pathName(pin), "", result);
}

void
ReportPath::reportEndpoint(const char *end, const char *reason, string &result)
{
  reportStartEndPoint(end, reason, "Endpoint", result);
}

void
ReportPath::reportStartEndPoint(const char *pt,
				const char *reason,
				const char *key,
				string &result)
{
  // Account for punctuation in the line.
  int line_len = strlen(key) + 2 + strlen(pt) + 2 + strlen(reason) + 1;
  if (!no_split_
      && line_len > start_end_pt_width_) {
    result += key;
    result += ": ";
    result += pt;
    reportEndOfLine(result);

    for (int i = 0; i < strlen(key); i++)
      result += ' ';

    result += "  (";
    result += reason;
    result += ")\n";
  }
  else {
    result += key;
    result += ": ";
    result += pt;
    result += " (";
    result += reason;
    result += ")\n";
  }
}

void
ReportPath::reportGroup(const PathEnd *end,
			string &result)
{
  result += "Path Group: ";
  result += search_->pathGroup(end)->name();
  reportEndOfLine(result);
  result += "Path Type: ";
  result += end->minMax(this)->asString();
  reportEndOfLine(result);
  if (corners_->multiCorner()) {
    result += "Corner: ";
    result += end->pathAnalysisPt(this)->corner()->name();
    reportEndOfLine(result);
  }
}

////////////////////////////////////////////////////////////////

const char *
ReportPath::checkRoleReason(const PathEnd *end)
{
  const char *setup_hold = end->checkRole(this)->asString();
  return stringPrintTmp(strlen(" time")
			+ strlen(setup_hold) + 1,
			"%s time",
			setup_hold);
}

const char *
ReportPath::tgtClkName(const PathEnd *end)
{
  ClockEdge *tgt_clk_edge = end->targetClkEdge(this);
  const Clock *tgt_clk = tgt_clk_edge->clock();
  const TransRiseFall *clk_tr = tgt_clk_edge->transition();
  const TransRiseFall *clk_end_tr = end->targetClkEndTrans(this);
  const char *clk_name = tgt_clk->name();
  if (clk_end_tr == clk_tr)
    return clk_name;
  else
    return clkNameInverted(clk_name);
}

const char *
ReportPath::clkNameInverted(const char *clk_name)
{
  return stringPrintTmp(strlen(clk_name) + 2, "%s'", clk_name);
}

const char *
ReportPath::clkRegLatchDesc(const PathEnd *end)
{
  // Goofy libraries can have registers with both rising and falling
  // clk->q timing arcs.  Try and match the timing check transition.
  const TransRiseFall *check_clk_tr=end->checkArc()->fromTrans()->asRiseFall();
  TimingArcSet *clk_set = NULL;
  TimingArcSet *clk_tr_set = NULL;
  Vertex *tgt_clk_vertex = end->targetClkPath()->vertex(this);
  VertexOutEdgeIterator iter(tgt_clk_vertex, graph_);
  while (iter.hasNext()) {
    Edge *edge = iter.next();
    TimingArcSet *arc_set = edge->timingArcSet();
    TimingRole *role = arc_set->role();
    if (role == TimingRole::regClkToQ()
	|| role == TimingRole::latchEnToQ()) {
      TransRiseFall *arc_tr = arc_set->isRisingFallingEdge();
      clk_set = arc_set;
      if (arc_tr == check_clk_tr)
	clk_tr_set = arc_set;
    }
  }
  if (clk_tr_set)
    return checkRegLatchDesc(clk_tr_set->role(),
			     clk_tr_set->isRisingFallingEdge());
  else if (clk_set)
    return checkRegLatchDesc(clk_set->role(),
			     clk_set->isRisingFallingEdge());
  else
    return checkRegLatchDesc(TimingRole::regClkToQ(), check_clk_tr);
}

void
ReportPath::reportSrcPathArrival(const PathEnd *end,
				 PathExpanded &expanded,
				 string &result)
{
  reportEndOfLine(result);
  reportSrcPath(end, expanded, result);
  reportLine("data arrival time", end->dataArrivalTimeOffset(this), result);
  reportEndOfLine(result);
}

void
ReportPath::reportSrcPath(const PathEnd *end,
			  PathExpanded &expanded,
			  string &result)
{
  reportPathHeader(result);
  float src_clk_offset = end->sourceClkOffset(this);
  Arrival src_clk_insertion = end->sourceClkInsertionDelay(this);
  Arrival src_clk_latency = end->sourceClkLatency(this);
  const Path *path = end->path();
  reportSrcClkAndPath(path, expanded, src_clk_offset, src_clk_insertion,
		      src_clk_latency, end->isPathDelay(), result);
}

void
ReportPath::reportSrcClkAndPath(const Path *path,
				PathExpanded &expanded,
				float time_offset,
				Arrival clk_insertion,
				Arrival clk_latency,
				bool is_path_delay,
				string &result)
{
  ClockEdge *clk_edge = path->clkEdge(this);
  if (clk_edge) {
    Clock *clk = clk_edge->clock();
    TransRiseFall *clk_tr = clk_edge->transition();
    float clk_time = clk_edge->time() + time_offset;
    if (clk == sdc_->defaultArrivalClock()) {
      if (!is_path_delay) {
	float clk_end_time = clk_time + time_offset;
	reportLine("clock (input port clock) (rise edge)",
		   clk_end_time, clk_end_time, result);
	reportLine(clkNetworkDelayIdealProp(false), 0.0, clk_end_time, result);
      }
      reportPath1(path, expanded, false, time_offset, result);
    }
    else {
      bool path_from_input = false;
      bool input_has_ref_path = false;
      Arrival clk_delay, clk_end_time;
      const MinMax *min_max = path->minMax(this);
      PathRef clk_path;
      expanded.clkPath(clk_path);
      const TransRiseFall *clk_end_tr;
      const char *clk_name;
      if (!clk_path.isNull()) {
	clk_end_time = search_->clkPathArrival(&clk_path) + time_offset;
	clk_delay = clk_end_time - clk_time;
	clk_end_tr = clk_path.transition(this);
	clk_name = (clk_end_tr == clk_tr)
	  ? clk->name()
	  : clkNameInverted(clk->name());
      }
      else {
	// Path from input port or clk used as data.
	clk_name = clk->name();
	clk_end_tr = clk_tr;
	clk_delay = clk_insertion + clk_latency;
	clk_end_time = clk_time + clk_delay;

	PathRef *first_path = expanded.startPath();
	InputDelay *input_delay = pathInputDelay(first_path);
	if (input_delay) {
	  path_from_input = true;
	  Pin *ref_pin = input_delay->refPin();
	  if (ref_pin && clk->isPropagated()) {
	    PathRef ref_path;
	    pathInputDelayRefPath(first_path, input_delay, ref_path);
	    if (!ref_path.isNull()) {
	      const Arrival &ref_end_time = ref_path.arrival(this);
	      clk_delay = ref_end_time - clk_time;
	      clk_end_time = ref_end_time + time_offset;
	      input_has_ref_path = true;
	    }
	  }
	}
      }

      bool clk_used_as_data = pathFromClkPin(expanded);
      bool is_prop = isPropagated(path);
      const EarlyLate *early_late = min_max;
      if (reportGenClkSrcPath(clk_path.isNull() ? NULL : &clk_path,
			      clk, clk_tr, min_max, early_late)
	  && !(path_from_input && !input_has_ref_path)) {
	reportClkLine(clk, clk_name, clk_end_tr, clk_time, min_max, result);
 	const PathAnalysisPt *path_ap = path->pathAnalysisPt(this);
	reportGenClkSrcAndPath(path, clk, clk_tr, early_late, path_ap,
 			       time_offset, time_offset, clk_used_as_data,
			       result);
      }
      else if (clk_used_as_data 
 	       && pathFromGenPropClk(path, path->minMax(this))) {
	reportClkLine(clk, clk_name, clk_end_tr, clk_time, min_max, result);
 	ClkInfo *clk_info = path->tag(search_)->clkInfo();
 	if (clk_info->isPropagated())
 	  reportClkSrcLatency(clk_insertion, clk_time, result);
	reportPath1(path, expanded, true, time_offset, result);
      }
      else if (is_prop
	       && reportClkPath()
	       && !(path_from_input && !input_has_ref_path)) {
 	reportClkLine(clk, clk_name, clk_end_tr, clk_time, min_max, result);
	reportClkSrcLatency(clk_insertion, clk_time, result);
 	reportPath1(path, expanded, false, time_offset, result);
      }
      else if (clk_used_as_data) {
	reportClkLine(clk, clk_name, clk_end_tr, clk_time, min_max, result);
 	if (clk_insertion > 0.0)
	  reportClkSrcLatency(clk_insertion, clk_time, result);
	reportPath1(path, expanded, true, time_offset, result);
      }
      else {
	if (is_path_delay) {
	  if (clk_delay > 0.0)
	    reportLine(clkNetworkDelayIdealProp(is_prop), clk_delay,
		       clk_end_time, result);
	}
	else {
	  reportClkLine(clk, clk_name, clk_end_tr, clk_time, min_max, result);
	  float clk_arrival = clk_end_time;
	  reportLine(clkNetworkDelayIdealProp(is_prop), clk_delay,
		     clk_arrival, result);
	}
	reportPath1(path, expanded, false, time_offset, result);
      }
    }
  }
  else
    reportPath1(path, expanded, false, time_offset, result);
}

void
ReportPath::reportTgtClk(const PathEnd *end,
			 string &result)
{
  reportTgtClk(end, 0.0, result);
}

void
ReportPath::reportTgtClk(const PathEnd *end,
			 float prev_time,
			 string &result)
{
  Clock *clk = end->targetClk(this);
  const Path *clk_path = end->targetClkPath();
  reportTgtClk(end, prev_time, isPropagated(clk_path, clk), result);
}

void
ReportPath::reportTgtClk(const PathEnd *end,
			 float prev_time,
			 bool is_prop,
			 string &result)
{
  float src_offset = end->sourceClkOffset(this);
  const ClockEdge *clk_edge = end->targetClkEdge(this);
  Clock *clk = clk_edge->clock();
  const TransRiseFall *clk_tr = clk_edge->transition();
  const TransRiseFall *clk_end_tr = end->targetClkEndTrans(this);
  const char *clk_name = (clk_end_tr == clk_tr)
    ? clk->name()
    : clkNameInverted(clk->name());
  float clk_time = prev_time
    + end->targetClkTime(this)
    + end->targetClkMcpAdjustment(this)
    + src_offset;
  Arrival clk_delay = end->targetClkDelay(this);
  Arrival clk_arrival = clk_time + clk_delay;
  PathAnalysisPt *path_ap = end->pathAnalysisPt(this)->tgtClkAnalysisPt();
  const MinMax *min_max = path_ap->pathMinMax();
  const Path *clk_path = end->targetClkPath();
  reportClkLine(clk, clk_name, clk_end_tr, prev_time, clk_time, min_max, result);
  TimingRole *check_role = end->checkRole(this);
  if (is_prop && reportClkPath()) {
    float time_offset = prev_time
      + end->targetClkOffset(this)
      + end->targetClkMcpAdjustment(this);
    const EarlyLate *early_late = check_role->tgtClkEarlyLate();
    if (reportGenClkSrcPath(clk_path, clk, clk_tr, min_max, early_late)) {
      float insertion_offset = 
	clk_path ? tgtClkInsertionOffet(clk_path, early_late, path_ap) : 0.0;
      reportGenClkSrcAndPath(clk_path, clk, clk_tr, early_late, path_ap,
			     time_offset, time_offset + insertion_offset,
			     false, result);
    }
    else {
      Arrival insertion = end->targetClkInsertionDelay(this);
      if (clk_path) {
	reportClkSrcLatency(insertion, clk_time, result);
	PathExpanded clk_expanded(clk_path, this);
	float insertion_offset = tgtClkInsertionOffet(clk_path, early_late,
						      path_ap);
	reportPath5(clk_path, clk_expanded, 0, clk_expanded.size() - 1, is_prop,
		    reportClkPath(), delay_zero, time_offset + insertion_offset,
		    result);
      }
      else {
	// Output departure.
	Arrival clk_arrival = clk_time + clk_delay;
	reportLine(clkNetworkDelayIdealProp(clk->isPropagated()),
		   clk_delay, clk_arrival, result);
      }
    }
    reportClkUncertainty(end, clk_arrival, result);
    reportCommonClkPessimism(end, clk_arrival, result);
  }
  else {
    reportLine(clkNetworkDelayIdealProp(is_prop), clk_delay, clk_arrival,
	       result);
    reportClkUncertainty(end, clk_arrival, result);
    reportCommonClkPessimism(end, clk_arrival, result);
    if (clk_path) {
      Vertex *clk_vertex = clk_path->vertex(this);
      reportLine(descriptionField(clk_vertex),
		 prev_time
		 + end->targetClkArrival(this)
		 + end->sourceClkOffset(this),
		 clk_end_tr, result);
    }
  }
}

float
ReportPath::tgtClkInsertionOffet(const Path *clk_path,
				 const EarlyLate *early_late,
				 PathAnalysisPt *path_ap)
{
  ClkInfo *clk_info = clk_path->clkInfo(this);
  const Pin *src_pin = clk_info->clkSrc();
  const ClockEdge *clk_edge = clk_info->clkEdge();
  const Clock *clk = clk_edge->clock();
  const TransRiseFall *clk_tr = clk_edge->transition();
  const MinMax *min_max = path_ap->pathMinMax();
  Arrival path_insertion = search_->clockInsertion(clk, src_pin, clk_tr,
						   min_max, min_max,
						   path_ap);
  Arrival tgt_insertion = search_->clockInsertion(clk, src_pin, clk_tr,
						  min_max, early_late,
						  path_ap);
  return delayAsFloat(tgt_insertion - path_insertion);
}

bool
ReportPath::pathFromGenPropClk(const Path *clk_path,
			       const EarlyLate *early_late)
{
  ClkInfo *clk_info = clk_path->tag(search_)->clkInfo();
  const ClockEdge *clk_edge = clk_info->clkEdge();
  if (clk_edge) {
    const Clock *clk = clk_edge->clock();
    float insertion;
    bool exists;
    sdc_->clockInsertion(clk, clk_info->clkSrc(),
				 clk_edge->transition(),
				 clk_path->minMax(this),
				 early_late,
				 insertion, exists);
    return !exists
      && clk->isGeneratedWithPropagatedMaster();
  }
  else
    return false;
}

bool
ReportPath::isGenPropClk(const Clock *clk,
			 const TransRiseFall *clk_tr,
			 const MinMax *min_max,
			 const EarlyLate *early_late)
{
  float insertion;
  bool exists;
  sdc_->clockInsertion(clk, clk->srcPin(), clk_tr,
			       min_max, early_late,
			       insertion, exists);
  return !exists
    && clk->isGeneratedWithPropagatedMaster();
}

void
ReportPath::reportClkLine(const Clock *clk,
			  const char *clk_name,
			  const TransRiseFall *clk_tr,
			  Arrival clk_time,
			  const MinMax *min_max,
			  string &result)
{
  reportClkLine(clk, clk_name, clk_tr, 0.0, clk_time, min_max, result);
}

void
ReportPath::reportClkLine(const Clock *clk,
			  const char *clk_name,
			  const TransRiseFall *clk_tr,
			  Arrival prev_time,
			  Arrival clk_time,
			  const MinMax *min_max,
			  string &result)
{
  const char *rise_fall = asRiseFall(clk_tr);
  const char *clk_msg = stringPrintTmp(strlen("clock  ( edge)")
				       + strlen(clk_name)
				       + strlen(rise_fall) + 1,
				       "clock %s (%s edge)",
				       clk_name,
				       rise_fall);
  if (clk->isPropagated())
    reportLine(clk_msg, clk_time - prev_time, clk_time, result);
  else {
    // Report ideal clock slew.
    float clk_slew = clk->slew(clk_tr, min_max);
    reportLine(clk_msg, clk_slew, clk_time - prev_time, clk_time, result);
  }
}

bool
ReportPath::reportGenClkSrcPath(const Path *clk_path,
				Clock *clk,
				const TransRiseFall *clk_tr,
				const MinMax *min_max,
				const EarlyLate *early_late)
{
  bool from_gen_prop_clk = clk_path
    ? pathFromGenPropClk(clk_path, early_late)
    : isGenPropClk(clk, clk_tr, min_max, early_late);
  return from_gen_prop_clk
    && format_ == report_path_full_clock_expanded;
}

void
ReportPath::reportGenClkSrcAndPath(const Path *path,
				   Clock *clk,
				   const TransRiseFall *clk_tr,
				   const EarlyLate *early_late,
				   const PathAnalysisPt *path_ap,
				   float time_offset,
				   float path_time_offset,
				   bool clk_used_as_data,
				   string &result)
{
  const Pin *clk_pin = path
    ? path->clkInfo(search_)->clkSrc()
    : clk->defaultPin();
  float gclk_time = clk->edge(clk_tr)->time() + time_offset;
  bool skip_first_path = reportGenClkSrcPath1(clk, clk_pin, clk_tr,
					      early_late, path_ap, gclk_time,
					      time_offset, clk_used_as_data,
					      result);
  if (path) {
    PathExpanded expanded(path, this);
    reportPath4(path, expanded, skip_first_path, false, clk_used_as_data,
		path_time_offset, result);
  }
}

bool
ReportPath::reportGenClkSrcPath1(Clock *clk,
				 const Pin *clk_pin,
				 const TransRiseFall *clk_tr,
				 const EarlyLate *early_late,
				 const PathAnalysisPt *path_ap,
				 float gclk_time,
				 float time_offset,
				 bool clk_used_as_data,
				 string &result)
{
  PathAnalysisPt *insert_ap = path_ap->insertionAnalysisPt(early_late);
  PathVertex src_path;
  search_->genclks()->srcPath(clk, clk_pin, clk_tr, insert_ap, src_path);
  if (!src_path.isNull()) {
    ClkInfo *src_clk_info = src_path.clkInfo(search_);
    ClockEdge *src_clk_edge = src_clk_info->clkEdge();
    Clock *src_clk = src_clk_info->clock();
    bool skip_first_path = false;
    const TransRiseFall *src_clk_tr = src_clk_edge->transition();
    const Pin *src_clk_pin = src_clk_info->clkSrc();
    if (src_clk->isGeneratedWithPropagatedMaster()
	&& src_clk_info->isPropagated()) {
      skip_first_path = reportGenClkSrcPath1(src_clk, src_clk_pin,
					     src_clk_tr, early_late, path_ap,
					     gclk_time, time_offset,
					     clk_used_as_data, result);
    }
    else {
      const Arrival insertion = search_->clockInsertion(src_clk, src_clk_pin,
							src_clk_tr,
							path_ap->pathMinMax(),
							early_late, path_ap);
      reportClkSrcLatency(insertion, gclk_time, result);
    }
    PathExpanded src_expanded(&src_path, this);
    if (clk->pllOut()) {
      reportPath4(&src_path, src_expanded, skip_first_path, true,
		  clk_used_as_data, gclk_time, result);
      const MinMax *min_max = path_ap->pathMinMax();
      PathAnalysisPt *pll_ap=path_ap->insertionAnalysisPt(min_max->opposite());
      Arrival pll_delay = search_->genclks()->pllDelay(clk, clk_tr, pll_ap);
      size_t path_length = src_expanded.size();
      if (path_length < 2)
	internalError("generated clock pll source path too short.\n");
      PathRef *path0 = src_expanded.path(path_length - 2);
      Arrival time0 = path0->arrival(this) + gclk_time;
      PathRef *path1 = src_expanded.path(path_length - 1);
      reportPathLine(path1, -pll_delay, time0 - pll_delay, "pll_delay", result);
    }
    else
      reportPath4(&src_path, src_expanded, skip_first_path, false,
		  clk_used_as_data, gclk_time, result);
    if (!clk->isPropagated())
      reportLine("clock network delay (ideal)", 0.0, src_path.arrival(this),
		 result);
  }
  else {
    if (clk->isPropagated())
      reportClkSrcLatency(0.0, gclk_time, result);
    else if (!clk_used_as_data)
      reportLine("clock network delay (ideal)", 0.0, gclk_time, result);
  }
  return !src_path.isNull();
}

void
ReportPath::reportClkSrcLatency(Arrival insertion,
				float clk_time,
				string &result)
{
  reportLine("clock source latency", insertion, clk_time + insertion, result);
}

void
ReportPath::reportPathLine(const Path *path,
			   Arrival incr,
			   Arrival time,
			   const char *line_case,
			   string &result)
{
  Vertex *vertex = path->vertex(this);
  Pin *pin = vertex->pin();
  const char *what = descriptionField(vertex);
  const TransRiseFall *tr = path->transition(this);
  bool is_driver = network_->isDriver(pin);
  DcalcAnalysisPt *dcalc_ap = path->pathAnalysisPt(this)->dcalcAnalysisPt();
  DcalcAPIndex ap_index = dcalc_ap->index();
  Slew slew = graph_->slew(vertex, tr, ap_index);
  float cap = field_blank_;
  // Don't show capacitance field for input pins.
  if (is_driver && field_capacitance_->enabled())
    cap = loadCap(pin, tr, dcalc_ap);
  reportLine(what, cap, slew, field_blank_,
	     incr, time, false, tr, line_case, result);
}

void
ReportPath::reportRequired(const PathEnd *end,
			   const char *margin_msg,
			   string &result)
{
  Required req_time = end->requiredTimeOffset(this);
  ArcDelay margin = end->margin(this);
  if (end->minMax(this) == MinMax::max())
    margin = -margin;
  reportLine(margin_msg, margin, req_time, result);
  reportLine("data required time", req_time, result);
  reportDashLine(result);
}

void
ReportPath::reportSlack(const PathEnd *end,
			string &result)
{
  reportLine("data required time", end->requiredTimeOffset(this), result);
  reportLineNegative("data arrival time", end->dataArrivalTimeOffset(this),
		     result);
  reportDashLine(result);
  reportSlack(end->slack(this), result);
}

void
ReportPath::reportSlack(Slack slack,
			string &result)
{
  if (slack < 0.0)
    reportLine("slack (VIOLATED)", slack, result);
  else
    reportLine("slack (MET)", slack, result);
}

void
ReportPath::reportCommonClkPessimism(const PathEnd *end,
				     Arrival &clk_arrival,
				     string &result)
{
  if (sdc_->crprEnabled()) {
    float pessimism = end->commonClkPessimism(this);
    clk_arrival += pessimism;
    reportLine("clock reconvergence pessimism", pessimism, clk_arrival, result);
  }
}

void
ReportPath::reportClkUncertainty(const PathEnd *end,
				 Arrival &clk_arrival,
				 string &result)
{
  float uncertainty = end->targetNonInterClkUncertainty(this);
  clk_arrival += uncertainty;
  if (uncertainty != 0.0)
    reportLine("clock uncertainty", uncertainty, clk_arrival, result);
  float inter_uncertainty = end->interClkUncertainty(this);
  clk_arrival += inter_uncertainty;
  if (inter_uncertainty != 0.0)
    reportLine("inter-clock uncertainty", inter_uncertainty, clk_arrival,
	       result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportPath(const PathEnd *end,
		       PathExpanded &expanded,
		       string &result)
{
  reportPathHeader(result);
  // Source clk offset for path delays removes clock phase time.
  float src_clk_offset = end->sourceClkOffset(this);
  reportPath1(end->path(), expanded, pathFromClkPin(expanded),
	      src_clk_offset, result);
}

void
ReportPath::reportPath(const Path *path,
		       string &result)
{
  reportPathHeader(result);
  PathExpanded expanded(path, this);
  reportSrcClkAndPath(path, expanded, 0.0, delay_zero, delay_zero,
		      false, result);
}

void
ReportPath::reportPath1(const Path *path,
			PathExpanded &expanded,
			bool clk_used_as_data,
			float time_offset,
			string &result)
{
  PathRef *d_path, *q_path;
  Edge *d_q_edge;
  expanded.latchPaths(d_path, q_path, d_q_edge);
  if (d_path) {
    Arrival latch_time_given, latch_enable_time;
    PathVertex latch_enable_path;
    latches_->latchTimeGivenToStartpoint(d_path, q_path, d_q_edge,
					 latch_time_given,
					 latch_enable_path);
    if (!latch_enable_path.isNull()) {
      latch_enable_time = search_->clkPathArrival(&latch_enable_path);
      if (reportClkPath()) {
	PathExpanded enable_expanded(&latch_enable_path, this);
	// Report the path to the latch enable.
	reportPath2(&latch_enable_path, enable_expanded, false,
		    time_offset, result);
      }
      Arrival time = latch_enable_time + latch_time_given;
      Arrival incr = latch_time_given;
      if (incr >= 0.0)
	reportLine("time given to startpoint", incr, time, result);
      else
	reportLine("time borrowed from startpoint", incr, time, result);
      // Override latch D arrival with enable + given.
      reportPathLine(expanded.path(0), delay_zero, time, "latch_D", result);
      bool propagated_clk = path->clkInfo(search_)->isPropagated();
      bool report_clk_path = path->isClock(search_) || reportClkPath();
      reportPath5(path, expanded, 1, expanded.size() - 1,
		  propagated_clk, report_clk_path,
		  latch_enable_time + latch_time_given, time_offset, result);
    }
  }
  else
    reportPath2(path, expanded, clk_used_as_data, time_offset, result);
}

void
ReportPath::reportPath2(const Path *path,
			PathExpanded &expanded,
			bool clk_used_as_data,
			float time_offset,
			string &result)
{
  // Report the clock path if the end is a clock or we wouldn't have
  // anything to report.
  bool report_clk_path = clk_used_as_data
    || (reportClkPath()
	&& path->clkInfo(search_)->isPropagated());
  reportPath3(path, expanded, clk_used_as_data, report_clk_path,
	      delay_zero, time_offset, result);
}

void
ReportPath::reportPath3(const Path *path,
			PathExpanded &expanded,
			bool clk_used_as_data,
			bool report_clk_path,
			Arrival prev_time,
			float time_offset,
			string &result)
{
  bool propagated_clk = clk_used_as_data
    || path->clkInfo(search_)->isPropagated();
  size_t path_last_index = expanded.size() - 1;
  reportPath5(path, expanded, 0, path_last_index, propagated_clk,
	      report_clk_path, prev_time, time_offset, result);
}

void
ReportPath::reportPath4(const Path *path,
			PathExpanded &expanded,
			bool skip_first_path,
			bool skip_last_path,
			bool clk_used_as_data,
			float time_offset,
			string &result)
{
  size_t path_first_index = 0;
  Arrival prev_time(0.0);
  if (skip_first_path) {
    path_first_index = 1;
    PathRef *start = expanded.path(0);
    prev_time = start->arrival(this) + time_offset;
  }
  size_t path_last_index = expanded.size() - 1;
  if (skip_last_path
      && path_last_index > 1)
    path_last_index--;
  bool propagated_clk = clk_used_as_data
    || path->clkInfo(search_)->isPropagated();
  // Report the clock path if the end is a clock or we wouldn't have
  // anything to report.
  bool report_clk_path = path->isClock(search_) 
    || (reportClkPath() && propagated_clk);
  reportPath5(path, expanded, path_first_index, path_last_index,
	      propagated_clk, report_clk_path, prev_time, time_offset, result);
}

void
ReportPath::reportPath5(const Path *path,
			PathExpanded &expanded,
			size_t path_first_index,
			size_t path_last_index,
			bool propagated_clk,
			bool report_clk_path,
			Arrival prev_time,
			float time_offset,
			string &result)
{
  const MinMax *min_max = path->minMax(this);
  DcalcAnalysisPt *dcalc_ap = path->pathAnalysisPt(this)->dcalcAnalysisPt();
  DcalcAPIndex ap_index = dcalc_ap->index();
  for (size_t i = path_first_index; i <= path_last_index; i++) {
    PathRef *path1 = expanded.path(i);
    TimingArc *prev_arc = expanded.prevArc(i);
    Vertex *vertex = path1->vertex(this);
    Pin *pin = vertex->pin();
    Arrival time = path1->arrival(this) + time_offset;
    Arrival incr(0.0);
    const char *line_case = NULL;
    bool is_clk_start = network_->isRegClkPin(pin);
    bool is_clk = path1->isClock(search_);
    // Always show the search start point (register clk pin).
    // Skip reporting the clk tree unless it is requested.
    if (is_clk_start
	|| report_clk_path
	|| !is_clk) {
      const TransRiseFall *tr = path1->transition(this);
      Slew slew = graph_->slew(vertex, tr, ap_index);
      if (prev_arc == NULL) {
	// First path.
	reportInputExternalDelay(path1, time_offset, result);
	size_t next_index = i + 1;
	const PathRef *next_path = expanded.path(next_index);
	if (network_->isTopLevelPort(pin)
	    && next_path
	    && !nextArcAnnotated(next_path, next_index, expanded, ap_index)
	    && hasExtInputDriver(pin, tr, min_max)) {
	  // Pin is an input port with drive_cell/drive_resistance.
	  // The delay calculator annotates wire delays on the edges
	  // from the input to the loads.  Report the wire delay on the
	  // input pin instead.
	  Arrival next_time = next_path->arrival(this) + time_offset;
	  incr = next_time - time;
	  time = next_time;
	  line_case = "input_drive";
	}
	else if (is_clk) {
	  if (!propagated_clk)
	    // Clock latency at path endpoint in case latency was set
	    // on a clock pin other than the clock source.
	    time = search_->clkPathArrival(path1) + time_offset;
	  incr = 0.0;
	  line_case = "clk_first";
	}
	else {
	  incr = 0.0;
	  line_case = "first";
	}
      }
      else if (is_clk_start
	       && is_clk
	       && !report_clk_path) {
	// Clock start point and clock path are not reported.
	incr = 0.0;
	if (!propagated_clk) {
	  // Ideal clock.
	  ClockEdge *src_clk_edge = path->clkEdge(this);
	  time = search_->clkPathArrival(path1) + time_offset;
	  if (src_clk_edge) {
	    Clock *src_clk = src_clk_edge->clock();
	    TransRiseFall *src_clk_tr = src_clk_edge->transition();
	    slew = src_clk->slew(src_clk_tr, min_max);
	  }
	}
	line_case = "clk_start";
      }
      else if (is_clk
	       && report_clk_path
	       && !propagated_clk) {
	// Zero the clock network delays for ideal clocks.
	incr = 0.0;
	time = prev_time;
	ClockEdge *src_clk_edge = path->clkEdge(this);
	Clock *src_clk = src_clk_edge->clock();
	TransRiseFall *src_clk_tr = src_clk_edge->transition();
	slew = src_clk->slew(src_clk_tr, min_max);
	line_case = "clk_ideal";
      }
      else if (is_clk && !is_clk_start) {
	incr = time - prev_time;
	line_case = "clk_prop";
      }
      else {
	incr = time - prev_time;
	line_case = "normal";
      }
      if (report_input_pin_
	  || (i == path_last_index)
	  || is_clk_start
	  || (prev_arc == NULL)
	  // Filter wire edges from report unless reporting
	  // input pins.
	  || (prev_arc
	      && !prev_arc->role()->isWire())) {
	bool is_driver = network_->isDriver(pin);
	float cap = field_blank_;
	// Don't show capacitance field for input pins.
	if (is_driver && field_capacitance_->enabled())
	  cap = loadCap(pin, tr, dcalc_ap);
	const char *what = descriptionField(vertex);
	if (report_net_ && is_driver) {
	  // Capacitance field is reported on the net line.
	  reportLine(what, field_blank_, slew, field_blank_,
		     incr, time, false, tr, line_case, result);
	  if (network_->isTopLevelPort(pin)) {
	    const char *pin_name = cmd_network_->pathName(pin);
	    what = stringPrintTmp(strlen(" (net)")
				  + strlen(pin_name) + 1,
				  "%s (net)", pin_name);
	  }
	  else {
	    Net *net = network_->net(pin);
	    if (net) {
	      Net *highest_net = network_->highestNetAbove(net);
	      const char *net_name = cmd_network_->pathName(highest_net);
	      what = stringPrintTmp(strlen(" (net)")
				    + strlen(net_name) + 1,
				    "%s (net)", net_name);
	    }
	    else
	      what = "(unconnected)";
	  }
	  float fanout = drvrFanout(vertex, min_max);
	  reportLine(what, cap, field_blank_, fanout,
		     field_blank_, field_blank_, false, NULL,
		     line_case, result);
	}
	else
	  reportLine(what, cap, slew, field_blank_,
		     incr, time, false, tr, line_case, result);
	prev_time = time;
      }
    }
    else
      prev_time = time;
  }
}

bool
ReportPath::nextArcAnnotated(const PathRef *next_path,
			     size_t next_index,
			     PathExpanded &expanded,
			     DcalcAPIndex ap_index)
{
  TimingArc *arc = expanded.prevArc(next_index);
  Edge *edge = next_path->prevEdge(arc, this);
  return edge->arcDelayAnnotated(arc, ap_index);
}

char *
ReportPath::descriptionField(Vertex *vertex)
{
  Pin *pin = vertex->pin();
  const char *pin_name = cmd_network_->pathName(pin);
  const char *name2;
  if (network_->isTopLevelPort(pin)) {
    PortDirection *dir = network_->direction(pin);
    // Translate port direction.  Note that this is intentionally
    // inconsistent with the direction reported for top level ports as
    // startpoints.
    if (dir->isInput())
      name2 = "in";
    else if (dir->isOutput() || dir->isTristate())
      name2 = "out";
    else if (dir->isBidirect())
      name2 = "inout";
    else
      name2 = "?";
  }
  else {
    Instance *inst = network_->instance(pin);
    name2 = network_->cellName(inst);
  }
  // stringPrintTmp("%s (%s)", pin_name, name2)
  size_t pin_name_length = strlen(pin_name);
  size_t name2_length = strlen(name2);
  char *result = makeTmpString(pin_name_length + name2_length
			       + 4 /* strlen(" ()") + '\0' */);
  char *s = result;
  strcpy(s, pin_name);
  s += pin_name_length;
  *s++ = ' ';
  *s++ = '(';
  strcpy(s, name2);
  s += name2_length;
  *s++ = ')';
  *s++ = '\0';
  return result;
}

float
ReportPath::drvrFanout(Vertex *drvr,
		       const MinMax *min_max)
{
  float fanout = 0.0;
  VertexOutEdgeIterator iter(drvr, graph_);
  while (iter.hasNext()) {
    Edge *edge = iter.next();
    Pin *pin = edge->to(graph_)->pin();
    if (network_->isTopLevelPort(pin)) {
      // Output port counts as a fanout.
      Port *port = network_->port(pin);
      fanout += sdc_->portExtFanout(port, min_max) + 1;
    }
    else
      fanout++;
  }
  return fanout;
}

bool
ReportPath::hasExtInputDriver(const Pin *pin,
			      const TransRiseFall *tr,
			      const MinMax *min_max)
{
  Port *port = network_->port(pin);
  InputDrive *drive = sdc_->findInputDrive(port);
  return (drive
	  && (drive->hasDriveResistance(tr, min_max)
	      || drive->hasDriveCell(tr, min_max)));
}

void
ReportPath::reportInputExternalDelay(const Path *first_path,
				     float time_offset,
				     string &result)
{
  const Pin *first_pin = first_path->pin(graph_);
  if (!pathFromClkPin(first_path, first_pin)) {
    const TransRiseFall *tr = first_path->transition(this);
    Arrival time = first_path->arrival(this) + time_offset;
    InputDelay *input_delay = pathInputDelay(first_path);
    if (input_delay) {
      const Pin *ref_pin = input_delay->refPin();
      if (ref_pin) {
	PathRef ref_path;
	pathInputDelayRefPath(first_path, input_delay, ref_path);
	if (!ref_path.isNull() && reportClkPath()) {
	  PathExpanded ref_expanded(&ref_path, this);
	  float ref_offset = time_offset;
	  ClkInfo *ref_clk_info = ref_path.clkInfo(this);
	  // Ref clk does not include latency for non-ideal clocks.
	  // Remove it to compensate for reportPath5 adding it.
	  if (!ref_clk_info->isPropagated())
	    ref_offset -= ref_clk_info->latency();
	  reportPath3(&ref_path, ref_expanded, false, true,
		      delay_zero, 0.0, result);
	}
      }
      float input_arrival =
	input_delay->delays()->value(tr, first_path->minMax(this));
      reportLine("input external delay", input_arrival, time, tr, result);
    }
    else if (network_->isTopLevelPort(first_pin))
      reportLine("input external delay", 0.0, time, tr, result);
  }
}

// Return the input delay at the start of a path.
InputDelay *
ReportPath::pathInputDelay(const Path *first_path) const
{
  return first_path->tag(this)->inputDelay();
}

void
ReportPath::pathInputDelayRefPath(const Path *path,
				  InputDelay *input_delay,
				  // Return value.
				  PathRef &ref_path)
{
  Pin *ref_pin = input_delay->refPin();
  TransRiseFall *ref_tr = input_delay->refTransition();
  Vertex *ref_vertex = graph_->pinDrvrVertex(ref_pin);
  const PathAnalysisPt *path_ap = path->pathAnalysisPt(this);
  const ClockEdge *clk_edge = path->clkEdge(this);
  VertexPathIterator path_iter(ref_vertex, ref_tr, path_ap, this);
  while (path_iter.hasNext()) {
    PathVertex *path = path_iter.next();
    if (path->isClock(this)
	&& path->clkEdge(this) == clk_edge) {
      ref_path.init(path);
      break;
    }
  }
}

float
ReportPath::loadCap(Pin *drvr_pin,
		    const TransRiseFall *tr,
		    DcalcAnalysisPt *dcalc_ap)
{
  Parasitic *parasitic = NULL;
  bool delete_parasitic;
  if (arc_delay_calc_)
    arc_delay_calc_->findParasitic(drvr_pin, tr, dcalc_ap,
				   parasitic, delete_parasitic);
  float cap = graph_delay_calc_->loadCap(drvr_pin, parasitic, tr, dcalc_ap);
  if (arc_delay_calc_)
    arc_delay_calc_->finish(drvr_pin, tr, dcalc_ap,
			    parasitic, delete_parasitic);
  return cap;
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportPathHeader(string &result)
{
  ReportFieldSeq::Iterator field_iter(fields_);
  bool first_field = true;
  while (field_iter.hasNext()) {
    ReportField *field = field_iter.next();
    if (field->enabled()) {
      if (!first_field)
	result += ' ';
      first_field = false;

      reportField(field->title(), field, result);
    }
  }
  trimRight(result);
  reportEndOfLine(result);

  reportDashLine(result);
}

// Report total.
void
ReportPath::reportLine(const char *what,
		       Delay total,
		       string &result)
{
  reportLine(what, field_blank_, field_blank_, field_blank_,
	     field_blank_, total, false, NULL, NULL, result);
}

// Report negative total.
void
ReportPath::reportLineNegative(const char *what,
			       Delay total,
			       string &result)
{
  reportLine(what, field_blank_, field_blank_, field_blank_,
	     field_blank_, total, true, NULL, NULL, result);
}

// Report total, and transition suffix.
void
ReportPath::reportLine(const char *what,
		       Delay total,
		       const TransRiseFall *tr,
		       string &result)
{
  reportLine(what, field_blank_, field_blank_, field_blank_,
	     field_blank_, total, false, tr, NULL, result);
}

// Report increment, and total.
void
ReportPath::reportLine(const char *what,
		       Delay incr,
		       Delay total,
		       string &result)
{
  reportLine(what, field_blank_, field_blank_, field_blank_,
	     incr, total, false, NULL, NULL, result);
}

// Report increment, total, and transition suffix.
void
ReportPath::reportLine(const char *what,
		       Delay incr,
		       Delay total,
		       const TransRiseFall *tr,
		       string &result)
{
  reportLine(what, field_blank_, field_blank_, field_blank_,
	     incr, total, false, tr, NULL, result);
}

// Report slew, increment, and total.
void
ReportPath::reportLine(const char *what,
		       Slew slew,
		       Delay incr,
		       Delay total,
		       string &result)
{
  reportLine(what, field_blank_, slew, field_blank_,
	     incr, total, false, NULL, NULL, result);
}

void
ReportPath::reportLine(const char *what,
		       float cap,
		       Slew slew,
		       float fanout,
		       Delay incr,
		       Delay total,
		       bool total_with_minus,
		       const TransRiseFall *tr,
		       const char *line_case,
		       string &result)
{
  ReportFieldSeq::Iterator field_iter(fields_);
  bool first_field = true;
  while (field_iter.hasNext()) {
    ReportField *field = field_iter.next();
    if (field->enabled()) {
      if (!first_field
	  // Compatibility kludge; suppress trailing whitespace for edge.
	  && field != field_edge_)
	result += ' ';
      first_field = false;

      if (field == field_description_)
	reportDescription(what, result);
      else if (field == field_fanout_) {
	if (fanout == field_blank_)
	  reportFieldBlank(field, result);
	else {
	  char *field = stringPrintTmp(field_fanout_->width() + 1, "%*d",
				       field_fanout_->width(),
				       static_cast<int>(fanout));
	  result += field;
	}
      }
      else if (field == field_capacitance_)
	reportField(cap, field, result);
      else if (field == field_slew_)
	reportField(slew, field, result);
      else if (field == field_incr_)
	reportField(incr, field, result);
      else if (field == field_total_) {
	if (total == field_blank_)
	  reportFieldBlank(field, result);
	else if (total_with_minus)
	  reportFieldTimeMinus(total, result);
	else
	  reportFieldTime(total, result);
      }
      else if (field == field_edge_) {
	if (tr) {
	  result += ' ';
	  reportField(tr->shortName(), field, result);
	}
	// Compatibility kludge; suppress trailing spaces.
	else if (field_iter.hasNext()) {
	  result += ' ';
	  reportFieldBlank(field, result);
	}
      }
      else if (field == field_case_ && line_case)
	result += line_case;
    }
  }
  reportEndOfLine(result);
}

////////////////////////////////////////////////////////////////

// Only the total field.
void
ReportPath::reportLineTotal(const char *what,
			    Delay incr,
			    string &result)
{
  reportLineTotal1(what, incr, false, result);
}

// Only the total field and always with leading minus sign.
void
ReportPath::reportLineTotalMinus(const char *what,
				 Delay decr,
				 string &result)
{
  reportLineTotal1(what, decr, true, result);
}

void
ReportPath::reportLineTotal1(const char *what,
			     Delay incr,
			     bool incr_with_minus,
			     string &result)
{
  reportDescription(what, result);
  result += ' ';
  if (incr_with_minus)
    reportFieldTimeMinus(incr, result);
  else
    reportFieldTime(incr, result);

  reportEndOfLine(result);
}

void
ReportPath::reportDashLineTotal(string &result)
{
  reportDashLine(field_description_->width() + field_total_->width() + 1,
		 result);
}

////////////////////////////////////////////////////////////////

void
ReportPath::reportDescription(const char *what,
			      string &result)
{
  result += what;
  int length = strlen(what);
  if (!no_split_ && length > field_description_->width()) {
    reportEndOfLine(result);
    for (int i = 0; i < field_description_->width(); i++)
      result += ' ';
  }
  else {
    for (int i = length; i < field_description_->width(); i++)
      result += ' ';
  }
}

void
ReportPath::reportSpaceFieldTime(Delay value,
				 string &result)
{
  result += ' ';
  reportFieldTime(value, result);
}

void
ReportPath::reportFieldTime(Delay value,
			    string &result)
{
  const char *field = units_->timeUnit()->asString(delayAsFloat(value),
						   digits_);
  if (stringEq(field, minus_zero_))
    // Filter "-0.00" fields.
    field = plus_zero_;
  reportField(field, field_total_, result);
}

// Total time always with leading minus sign.
void
ReportPath::reportFieldTimeMinus(Delay value,
				 string &result)
{
  const char *field=units_->timeUnit()->asString(-delayAsFloat(value),digits_);
  if (stringEq(field, plus_zero_))
    // Force leading minus sign.
    field = minus_zero_;
  reportField(field, field_total_, result);
}

void
ReportPath::reportField(float value,
			ReportField *field,
			string &result)
{
  if (value == field_blank_)
    reportFieldBlank(field, result);
  else {
    const char *value_str = field->unit()->asString(value, digits_);
    reportField(value_str, field, result);
  }
}

void
ReportPath::reportField(const char *value,
			ReportField *field,
			string &result)
{
  if (field->leftJustify())
    result += value;
  for (int i = strlen(value); i < field->width(); i++)
    result += ' ';
  if (!field->leftJustify())
    result += value;
}

void
ReportPath::reportFieldBlank(ReportField *field,
			     string &result)
{
  result += field->blank();
}

void
ReportPath::reportDashLine(string &result)
{
  ReportFieldSeq::Iterator field_iter(fields_);
  while (field_iter.hasNext()) {
    ReportField *field = field_iter.next();
    if (field->enabled()) {
      for (int i = 0; i < field->width(); i++)
	result += '-';
    }
  }
  result += "------";
  reportEndOfLine(result);
}

void
ReportPath::reportDashLine(int line_width,
			   string &result)
{
  for (int i = 0; i < line_width; i++)
    result += '-';
  reportEndOfLine(result);
}

void
ReportPath::reportEndOfLine(string &result)
{
  result += '\n';
}

bool
ReportPath::reportClkPath() const
{
  return format_ == report_path_full_clock
    || format_ == report_path_full_clock_expanded;
}

////////////////////////////////////////////////////////////////

const char *
ReportPath::asRisingFalling(const TransRiseFall *tr)
{
  if (tr == TransRiseFall::rise())
    return "rising";
  else
    return "falling";
}

const char *
ReportPath::asRiseFall(const TransRiseFall *tr)
{
  if (tr == TransRiseFall::rise())
    return "rise";
  else
    return "fall";
}

// Find the startpoint type from the first path edge.
const char *
ReportPath::edgeRegLatchDesc(Edge *first_edge,
			     TimingArc *first_arc)
{
  TimingRole *role = first_arc->role();
  if (role == TimingRole::latchDtoQ()) {
    Instance *inst = network_->instance(first_edge->to(graph_)->pin());
    LibertyCell *cell = network_->libertyCell(inst);
    if (cell) {
      LibertyPort *enable_port;
      FuncExpr *enable_func;
      TransRiseFall *enable_tr;
      cell->latchEnable(first_edge->timingArcSet(),
			enable_port, enable_func, enable_tr);
      return latchDesc(enable_tr);
    }
  }
  else if (role == TimingRole::regClkToQ())
    return regDesc(first_arc->fromTrans()->asRiseFall());
  else if (role == TimingRole::latchEnToQ())
    return latchDesc(first_arc->fromTrans()->asRiseFall());
  // Who knows...
  return regDesc(first_arc->fromTrans()->asRiseFall());
}

const char *
ReportPath::checkRegLatchDesc(const TimingRole *role,
			      const TransRiseFall *clk_tr) const
{
  if (role == TimingRole::regClkToQ())
    return regDesc(clk_tr);
  else if (role == TimingRole::latchEnToQ()
	   || role == TimingRole::latchDtoQ())
    return latchDesc(clk_tr);
  else
    // Default when we don't know better.
    return "edge-triggered flip-flop";
}

const char *
ReportPath::regDesc(const TransRiseFall *clk_tr) const
{
  if (clk_tr == TransRiseFall::rise())
    return "rising edge-triggered flip-flop";
  else if (clk_tr == TransRiseFall::fall())
    return "falling edge-triggered flip-flop";
  else
    return "edge-triggered flip-flop";
}

const char *
ReportPath::latchDesc(const TransRiseFall *clk_tr) const
{
  return (clk_tr == TransRiseFall::rise()) 
    ? "positive level-sensitive latch"
    : "negative level-sensitive latch";
}

} // namespace
