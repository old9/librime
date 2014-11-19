//
// Copyleft RIME Developers
// License: GPLv3
//
// 2014-11-19 Chen Gong <chen.sst@gmail.com>
//
#ifndef RIME_SINGLE_CHAR_FILTER_H_
#define RIME_SINGLE_CHAR_FILTER_H_

#include <rime/filter.h>

namespace rime {

class SingleCharFilter : public Filter {
 public:
  explicit SingleCharFilter(const Ticket& ticket);

  virtual void Apply(CandidateList* recruited,
                     CandidateList* candidates);
};

}  // namespace rime

#endif  // RIME_SINGLE_CHAR_FILTER_H_
