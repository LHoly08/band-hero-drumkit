#include "Web.hpp"

namespace bh {

static constexpr unsigned char indexHtml[]{
#embed "web/index.html"
};

static constexpr unsigned char appJs[]{
#embed "web/app.js"
};

static constexpr unsigned char styleCss[]{
#embed "web/style.css"
};

bh::Web::Web() {}

bh::Web::~Web() {}

} // namespace bh
