#ifndef LOCALTOOLSOLS_H
#define LOCALTOOLS_H

#include <httplib.h>

namespace routes::localTools {

void registerLocalTools( httplib::Server &svr );

} // namespace routes

#endif // LOCALTOOLS_H
