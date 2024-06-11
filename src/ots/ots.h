#pragma once

#include <cinttypes>
#include <string>
#include <vector>
#include "cryptoTools/Network/Channel.h"
#include "cryptoTools/Network/IOService.h"
#include "cryptoTools/Network/Session.h"

#include "libOTe/Base/BaseOT.h"
#include "libOTe/NChooseOne/Kkrt/KkrtNcoOtReceiver.h"
#include "libOTe/NChooseOne/Kkrt/KkrtNcoOtSender.h"
#include "common/config.h"
#include "common/constants.h"

namespace ENCRYPTO {

std::vector<osuCrypto::block> ot_receiver(const std::vector<std::uint64_t>& inputs, osuCrypto::Channel& recvChl,
                                       ENCRYPTO::PsiAnalyticsContext& context);

std::vector<std::vector<osuCrypto::block>> ot_sender(
    const std::vector<std::vector<std::uint64_t>>& inputs, osuCrypto::Channel& sendChl, ENCRYPTO::PsiAnalyticsContext& context);
}
