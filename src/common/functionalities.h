#pragma once

#include "abycore/aby/abyparty.h"
#include "abycore/circuit/share.h"
#include "helpers.h"
#include "config.h"
#include "EzPC/SCI/src/utils/emp-tool.h"
#include "ots/ots.h"

#include <vector>

#define C_CONST 8459320670953116686
#define S_CONST 18286333650295995643
namespace ENCRYPTO {

void run_circuit_psi(const std::vector<std::uint64_t> &inputs, PsiAnalyticsContext &context, std::unique_ptr<CSocket> &sock, sci::NetIO* ioArr[2], osuCrypto::Channel &chl);

std::unique_ptr<CSocket> EstablishConnection(const std::string &address, uint16_t port,
                                             e_role role);

std::size_t PlainIntersectionSize(std::vector<std::uint64_t> v1, std::vector<std::uint64_t> v2);

void PrintTimings(const PsiAnalyticsContext &context);
void PrintCommunication(const PsiAnalyticsContext &context);

void ResetCommunication(std::unique_ptr<CSocket> &sock, osuCrypto::Channel &chl, sci::NetIO* ioArr[2], PsiAnalyticsContext &context);
void AccumulateCommunicationPSI(std::unique_ptr<CSocket> &sock, osuCrypto::Channel &chl, sci::NetIO* ioArr[2], PsiAnalyticsContext &context);
void PrintCommunication(PsiAnalyticsContext &context);
}
