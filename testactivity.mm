#include "testactivity.h"

#include <QtGlobal>

#ifdef Q_OS_MACOS
#import <Foundation/Foundation.h>

void beginTestActivity()
{
    static id activity = nil;   // tenuta per tutta la vita del processo
    if (activity) return;
    activity = [[[NSProcessInfo processInfo]
        beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
                          reason:@"SurfaceExplorer self-test"] retain];
}
#else
void beginTestActivity() {}
#endif
