#pragma once
#include "ordered.hpp"
#include "coverage.h"
namespace ass::graphics {
inline void submit_backend(ag_handle display,const ag_ordered_backend_snapshot &snapshot) {
    check(ag_ordered_submit_backend(display,&snapshot));
}
inline ag_coverage_metrics coverage_metrics(ag_handle display) {
    ag_coverage_metrics value{sizeof(value),1};
    check(ag_ordered_coverage_metrics(display,&value));return value;
}
}
