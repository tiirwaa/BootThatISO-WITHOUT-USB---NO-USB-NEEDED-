#ifndef SPACEVALIDATIONRESULT_H
#define SPACEVALIDATIONRESULT_H

#include <string>

struct SpaceValidationResult {
    bool        isValid;
    long long   availableGB;
    std::string errorMessage;
};

#endif // SPACEVALIDATIONRESULT_H