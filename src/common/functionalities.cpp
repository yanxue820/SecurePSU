#include "functionalities.h"

#include "ENCRYPTO_utils/connection.h"
#include "ENCRYPTO_utils/socket.h"
#include "abycore/sharing/boolsharing.h"
#include "abycore/sharing/sharing.h"
// #include "polynomials/Poly.h"

#include "HashingTables/cuckoo_hashing/cuckoo_hashing.h"
#include "HashingTables/common/hash_table_entry.h"
#include "HashingTables/common/hashing.h"
#include "HashingTables/simple_hashing/simple_hashing.h"
#include "config.h"
#include "batch_equality.h"
#include "equality.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <ratio>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include "table_opprf.h"

#include "OSNReceiver.h"
#include "OSNSender.h"

#include <openssl/sha.h>

using namespace osuCrypto;
using namespace std;

struct hashlocmap
{
  int bin;
  int index;
};

std::vector<uint64_t> content_of_bins;

namespace ENCRYPTO
{

  using share_ptr = std::shared_ptr<share>;

  using milliseconds_ratio = std::ratio<1, 1000>;
  using duration_millis = std::chrono::duration<double, milliseconds_ratio>;

  void run_circuit_psi(const std::vector<std::uint64_t> &inputs, PsiAnalyticsContext &context, std::unique_ptr<CSocket> &sock,
                       sci::NetIO *ioArr[2], osuCrypto::Channel &chl)
  {
    int party = 1;
    if (context.role == 0)
    {
      party = 2;
    }

    sci::OTPack<sci::NetIO> *otpackArr[2];

    // Config
    int l = (int)context.bitlen;
    int b = (int)context.radix;

    int num_cmps, rmdr;
    rmdr = context.nbins % 8;
    num_cmps = context.nbins + rmdr;
    int pad;
    uint64_t value;
    if (context.role == 0)
    {
      pad = rmdr;
      value = S_CONST;
    }
    else
    {
      pad = 3 * rmdr;
      value = C_CONST;
    }

    uint8_t *res_shares;

    if (context.role == CLIENT)
    {
      std::vector<std::vector<uint64_t>> opprf_values(context.nbins, std::vector<uint64_t>(context.ffuns));
      const auto clock_time_total_start = std::chrono::system_clock::now();
      content_of_bins.reserve(3 * num_cmps);

      // Hashing Phase
      const auto hashing_start_time = std::chrono::system_clock::now();
      ENCRYPTO::CuckooTable cuckoo_table(static_cast<std::size_t>(context.nbins));
      cuckoo_table.SetNumOfHashFunctions(context.nfuns);
      cuckoo_table.Insert(inputs);
      cuckoo_table.MapElements();

      if (cuckoo_table.GetStashSize() > 0u)
      {
        std::cerr << "[Error] Stash of size " << cuckoo_table.GetStashSize() << " occured\n";
      }

      auto cuckoo_table_v = cuckoo_table.AsRawVector();
      const auto hashing_end_time = std::chrono::system_clock::now();
      const duration_millis hashing_duration = hashing_end_time - hashing_start_time;
      context.timings.hashing = hashing_duration.count();

      // OPRF Phase
      auto masks_with_dummies = ot_receiver(cuckoo_table_v, chl, context);

      // Hint Computation Phase
      std::vector<uint64_t> garbled_cuckoo_filter;
      garbled_cuckoo_filter.reserve(context.fbins);

      const auto ftrans_start_time = std::chrono::system_clock::now();
      sock->Receive(garbled_cuckoo_filter.data(), context.fbins * sizeof(uint64_t));
      const auto ftrans_end_time = std::chrono::system_clock::now();
      const duration_millis hint_trans = ftrans_end_time - ftrans_start_time;
      context.timings.hint_transmission = hint_trans.count();

      const auto filter_start_time = std::chrono::system_clock::now();

      ENCRYPTO::CuckooTable garbled_cuckoo_table(static_cast<std::size_t>(context.fbins));
      garbled_cuckoo_table.SetNumOfHashFunctions(context.ffuns);
      garbled_cuckoo_table.Insert(cuckoo_table_v);
      auto addresses = garbled_cuckoo_table.GetElementAddresses();

      if (context.psm_type == PsiAnalyticsContext::PSM1)
      {
        for (int i = 0; i < context.nbins; i++)
        {
          osuCrypto::PRNG prngo(masks_with_dummies[i], 2);
          for (int j = 0; j < context.ffuns; j++)
          {
            content_of_bins[i * context.ffuns + j] = garbled_cuckoo_filter[addresses[i * context.ffuns + j]] ^ prngo.get<uint64_t>();
          }
        }
      }
      else
      {
        for (int i = 0; i < context.nbins; i++)
        {
          osuCrypto::PRNG prngo(masks_with_dummies[i], 2);
          for (int j = 0; j < context.ffuns; j++)
          {
            opprf_values[i][j] = garbled_cuckoo_filter[addresses[i * context.ffuns + j]] ^ prngo.get<uint64_t>();
          }
        }
      }

      const auto filter_end_time = std::chrono::system_clock::now();
      const duration_millis hint_duration = filter_end_time - filter_start_time;
      context.timings.hint_computation = hint_duration.count();

      res_shares = new uint8_t[num_cmps];
      for (int i = 0; i < pad; i++)
      {
        content_of_bins[3 * context.nbins + i] = value;
      }

      // PSM Phase
      const auto baseots_start_time = std::chrono::system_clock::now();
      otpackArr[0] = new OTPack<NetIO>(ioArr[0], party, b, l);
      otpackArr[1] = new OTPack<NetIO>(ioArr[1], 3 - party, b, l);
      const auto baseots_end_time = std::chrono::system_clock::now();
      const duration_millis base_ots_duration = baseots_end_time - baseots_start_time;
      context.timings.base_ots_sci = base_ots_duration.count();

      const auto clock_time_cir_start = std::chrono::system_clock::now();
      if (context.psm_type == PsiAnalyticsContext::PSM1)
      {
        BatchEquality<NetIO> *compare;
        compare = new BatchEquality<NetIO>(party, l, b, 3, num_cmps, ioArr[0], ioArr[1], otpackArr[0], otpackArr[1]);
        perform_batch_equality(content_of_bins.data(), compare, res_shares);
      }
      else
      {
        const int ts = 4;
        auto table_masks = ot_sender(opprf_values, chl, context);
        uint64_t bufferlength = (uint64_t)ceil(context.nbins / 2.0);
        osuCrypto::PRNG tab_prng(osuCrypto::sysRandomSeed(), bufferlength);

        content_of_bins.reserve(num_cmps);
        for (int i = 0; i < context.nbins; i++)
        {
          content_of_bins[i] = tab_prng.get<uint64_t>();
        }

        std::vector<osuCrypto::block> padding_vals;
        padding_vals.reserve(num_cmps);
        std::vector<uint64_t> table_opprf;
        table_opprf.reserve(ts * num_cmps);
        osuCrypto::PRNG padding_prng(osuCrypto::sysRandomSeed(), 2 * num_cmps);

        bufferlength = (uint64_t)ceil(context.nbins / 2.0);
        osuCrypto::PRNG dummy_prng(osuCrypto::sysRandomSeed(), bufferlength);

        // Get addresses
        uint64_t addresses1[context.ffuns];
        uint8_t bitaddress[context.ffuns];
        uint8_t bitindex[ts];
        uint64_t mask_ad = (1ULL << 2) - 1;

        double ave_ctr = 0.0;

        for (int i = 0; i < context.nbins; i++)
        {
          bool uniqueMap = false;
          int ctr = 0;
          while (!uniqueMap)
          {
            auto nonce = padding_prng.get<osuCrypto::block>();

            for (int j = 0; j < context.ffuns; j++)
            {
              addresses1[j] = hashToPosition(reinterpret_cast<uint64_t *>(&table_masks[i][j])[0], nonce);
              bitaddress[j] = addresses1[j] & mask_ad;
            }

            uniqueMap = true;
            for (int j = 0; j < ts; j++)
              bitindex[j] = ts;

            for (uint8_t j = 0; j < context.ffuns; j++)
            {
              if (bitindex[bitaddress[j]] != ts)
              {
                uniqueMap = false;
                break;
              }
              else
              {
                bitindex[bitaddress[j]] = j;
              }
            }

            if (uniqueMap)
            {
              padding_vals.push_back(nonce);
              for (int j = 0; j < ts; j++)
                if (bitindex[j] != -1)
                {
                  table_opprf[i * ts + j] = reinterpret_cast<uint64_t *>(&table_masks[i][bitindex[j]])[0] ^ content_of_bins[i];
                }
                else
                {
                  table_opprf[i * ts + j] = dummy_prng.get<uint64_t>();
                }
              ave_ctr += ctr;
            }
            ctr++;
          }
        }

        ave_ctr = ave_ctr / context.nbins;

        // Send nonces
        sock->Send(padding_vals.data(), context.nbins * sizeof(osuCrypto::block));
        // Send table
        sock->Send(table_opprf.data(), context.nbins * ts * sizeof(uint64_t));

        res_shares = new uint8_t[num_cmps];
        for (int i = 0; i < pad; i++)
        {
          content_of_bins[context.nbins + i] = value;
        }

        perform_equality(content_of_bins.data(), party, context.bitlen, b, num_cmps, context.address, context.port, res_shares, ioArr, otpackArr,context);

        //******************** ADD FOR OUR WORK ***********************//

        auto initializeVector = [&num_cmps]() -> std::vector<std::vector<std::uint64_t>>
        {
          std::vector<std::vector<std::uint64_t>> temp(num_cmps);
          for (int i = 0; i < num_cmps; ++i)
          {

            temp[i].push_back(0);
            // cout<<temp[i][0]<<"\n";
            temp[i].push_back(1);
            // cout<<temp[i][1]<<"\n";
          }
          return temp;
        };

        const std::vector<std::vector<std::uint64_t>> OT_sdr_inputs = initializeVector();

        std::vector<std::vector<osuCrypto::block>> item_encrypt_masks;
        item_encrypt_masks = ot_sender(OT_sdr_inputs, chl, context);

        OSNSender osn;
        const auto osnsetup_start_time = std::chrono::system_clock::now();
        osn.init(static_cast<size_t>(num_cmps), 1, "benes");
        const auto osnsetup_end_time = std::chrono::system_clock::now();
        const duration_millis osnsetup_duration = osnsetup_end_time - osnsetup_start_time;
        context.timings.osn_setup = osnsetup_duration.count();

        std::vector<oc::block> sender_shares;
        std::vector<oc::Channel> chls;
        chls.push_back(chl);
        sender_shares = osn.run_osn(chls);
         
        cout<<"start our work part-send ciphertext!\n"; 
        vector<int> permutation;
        permutation = osn.dest;

        std::vector<oc::block> masks;
        std::vector<oc::block> ciphertexts;

        for (int i = 0; i < num_cmps; ++i)
        {

          masks.push_back(item_encrypt_masks[permutation[i]][res_shares[permutation[i]]] ^ sender_shares[i]);

          osuCrypto::block blockValue = osuCrypto::toBlock(0, cuckoo_table_v[permutation[i]]);
          ciphertexts.push_back(blockValue ^ masks[i]);
          // cout<<i<<": "<<cuckoo_table_v[permutation[i]]<<"\n";

          // cout << i << ": " << static_cast<int>(res_shares[i]) << ": " << item_encrypt_masks[i][res_shares[i]] << ": " << sender_shares[i] << ": " << permutation[i] << ": " << masks[i] << "\n";

        }
        //  Send the actual data
        //chlfinal.send(ciphertexts.data(), num_cmps * sizeof(osuCrypto::block));
        chl.send(ciphertexts);
        cout<<"finish our work part!\n";

        //******************** ADD FOR OUR WORK ***********************//
      }
      const auto clock_time_cir_end = std::chrono::system_clock::now();
      const duration_millis cir_duration = clock_time_cir_end - clock_time_cir_start;
      context.timings.psm_time = cir_duration.count();

      const auto clock_time_total_end = std::chrono::system_clock::now();
      const duration_millis total_duration = clock_time_total_end - clock_time_total_start;
      context.timings.total = total_duration.count();
    }
    else
    { // Server
      cout<<"start putting to table!\n";
      content_of_bins.reserve(num_cmps);
      const auto clock_time_total_start = std::chrono::system_clock::now();

      // Hashing Phase
      const auto hashing_start_time = std::chrono::system_clock::now();

      ENCRYPTO::SimpleTable simple_table(static_cast<std::size_t>(context.nbins));
      simple_table.SetNumOfHashFunctions(context.nfuns);
      simple_table.Insert(inputs);
      simple_table.MapElements();
      // simple_table.Print();

      auto simple_table_v = simple_table.AsRaw2DVector();
      const auto hashing_end_time = std::chrono::system_clock::now();
      const duration_millis hashing_duration = hashing_end_time - hashing_start_time;
      context.timings.hashing = hashing_duration.count();

      auto masks = ot_sender(simple_table_v, chl, context);
      cout<<"start Hint!\n";
      // Hint Computation
      const auto filter_start_time = std::chrono::system_clock::now();
      uint64_t bufferlength = (uint64_t)ceil(context.nbins / 2.0);
      osuCrypto::PRNG prng(osuCrypto::sysRandomSeed(), bufferlength);

      for (int i = 0; i < context.nbins; i++)
      {
        content_of_bins.push_back(prng.get<uint64_t>());
      }

      std::unordered_map<uint64_t, hashlocmap> tloc;
      std::vector<uint64_t> filterinputs;
      for (int i = 0; i < context.nbins; i++)
      {
        int binsize = simple_table_v[i].size();
        for (int j = 0; j < binsize; j++)
        {
          tloc[simple_table_v[i][j]].bin = i;
          tloc[simple_table_v[i][j]].index = j;
          filterinputs.push_back(simple_table_v[i][j]);
        }
      }

      //cout<<"start 1!\n";
      ENCRYPTO::CuckooTable cuckoo_table(static_cast<std::size_t>(context.fbins));
      cuckoo_table.SetNumOfHashFunctions(context.ffuns);
      cuckoo_table.Insert(filterinputs);
      cuckoo_table.MapElements();
      // cuckoo_table.Print();

      if (cuckoo_table.GetStashSize() > 0u)
      {
        std::cerr << "[Error] Stash of size " << cuckoo_table.GetStashSize() << " occured\n";
      }
      //cout<<"start 2!\n";
      std::vector<uint64_t> garbled_cuckoo_filter;
      garbled_cuckoo_filter.reserve(context.fbins);

      bufferlength = (uint64_t)ceil(context.fbins - 3 * context.nbins);
      //cout<<"bufferlength: "<<bufferlength<<"\n";
      osuCrypto::PRNG prngo(osuCrypto::sysRandomSeed(), bufferlength);

      //cout<<"start 3!\n";

      for (int i = 0; i < context.fbins; i++)
      {
        if (!cuckoo_table.hash_table_.at(i).IsEmpty())
        {
          uint64_t element = cuckoo_table.hash_table_.at(i).GetElement();
          uint64_t function_id = cuckoo_table.hash_table_.at(i).GetCurrentFunctinId();
          hashlocmap hlm = tloc[element];
          osuCrypto::PRNG prng(masks[hlm.bin][hlm.index], 2);
          uint64_t pad = 0u;
          for (int j = 0; j <= function_id; j++)
          {
            pad = prng.get<uint64_t>();
          }
          garbled_cuckoo_filter[i] = content_of_bins[hlm.bin] ^ pad;
        }
        else
        {
          garbled_cuckoo_filter[i] = prngo.get<uint64_t>();
        }
        
      }
      //cout<<"start 4!\n";
      const auto filter_end_time = std::chrono::system_clock::now();
      const duration_millis hint_duration = filter_end_time - filter_start_time;
      context.timings.hint_computation = hint_duration.count();

      const auto ftrans_start_time = std::chrono::system_clock::now();
      sock->Send(garbled_cuckoo_filter.data(), context.fbins * sizeof(uint64_t));
      const auto ftrans_end_time = std::chrono::system_clock::now();
      const duration_millis hint_trans = ftrans_end_time - ftrans_start_time;
      context.timings.hint_transmission = hint_trans.count();

      res_shares = new uint8_t[num_cmps];
      for (int i = 0; i < pad; i++)
      {
        content_of_bins[context.nbins + i] = value;
      }

      const auto baseots_start_time = std::chrono::system_clock::now();
      otpackArr[0] = new OTPack<NetIO>(ioArr[0], party, b, l);
      otpackArr[1] = new OTPack<NetIO>(ioArr[1], 3 - party, b, l);
      const auto baseots_end_time = std::chrono::system_clock::now();
      const duration_millis base_ots_duration = baseots_end_time - baseots_start_time;
      context.timings.base_ots_sci = base_ots_duration.count();

      const auto clock_time_cir_start = std::chrono::system_clock::now();
      if (context.psm_type == PsiAnalyticsContext::PSM1)
      {
        BatchEquality<NetIO> *compare;
        compare = new BatchEquality<NetIO>(party, l, b, 3, num_cmps, ioArr[0], ioArr[1], otpackArr[0], otpackArr[1]);
        perform_batch_equality(content_of_bins.data(), compare, res_shares);
      }
      else
      {
        const int ts = 4;
        auto masks_with_dummies = ot_receiver(content_of_bins, chl, context);

        std::vector<osuCrypto::block> padding_vals;
        padding_vals.reserve(num_cmps);
        std::vector<uint64_t> table_opprf;
        table_opprf.reserve(ts * num_cmps);

        // Receive nonces
        sock->Receive(padding_vals.data(), context.nbins * sizeof(osuCrypto::block));
        // Receive table
        sock->Receive(table_opprf.data(), context.nbins * ts * sizeof(uint64_t));

        uint64_t addresses1;
        uint8_t bitaddress;
        uint64_t mask_ad = (1ULL << 2) - 1;
        std::vector<uint64_t> actual_contents_of_bins;
        actual_contents_of_bins.reserve(num_cmps);

        for (int i = 0; i < context.nbins; i++)
        {
          addresses1 = hashToPosition(reinterpret_cast<uint64_t *>(&masks_with_dummies[i])[0], padding_vals[i]);
          bitaddress = addresses1 & mask_ad;
          actual_contents_of_bins[i] = reinterpret_cast<uint64_t *>(&masks_with_dummies[i])[0] ^ table_opprf[ts * i + bitaddress];
        }

        for (int i = 0; i < pad; i++)
        {
          actual_contents_of_bins[context.nbins + i] = value;
        }

        // perform_batch_equality(content_of_bins.data(), compare, res_shares);
        res_shares = new uint8_t[num_cmps];
        perform_equality(actual_contents_of_bins.data(), party, context.bitlen, b, num_cmps, context.address, context.port, res_shares, ioArr, otpackArr,context);
  
        //******************** ADD FOR OUR WORK ***********************//
        cout<<"start our work part!\n";
        auto initializeVector = [&res_shares, &num_cmps]() -> std::vector<std::uint64_t>
        {
          std::vector<std::uint64_t> temp(num_cmps);
          for (int i = 0; i < num_cmps; i++)
          {
            temp[i] = static_cast<uint64_t>(res_shares[i]);
            //cout << i << ": " << temp[i] << "\n";
          }
          return temp;
        };

        const std::vector<std::uint64_t> OT_rcv_inputs = initializeVector();


        auto item_decrypt_masks = ot_receiver(OT_rcv_inputs, chl, context);  // if not \in, get the real mask, otherwise, get another mask;
        
   
        //cout<<"start our work part-OSN!\n";
        // share and shuffle the masks;
        //OSNReceiver* osn = new OSNReceiver();
        OSNReceiver osn;
        const auto osnsetup_start_time = std::chrono::system_clock::now();
        osn.init(static_cast<size_t>(num_cmps), 1);
        const auto osnsetup_end_time = std::chrono::system_clock::now();
        const duration_millis osnsetup_duration = osnsetup_end_time - osnsetup_start_time;
        context.timings.osn_setup = osnsetup_duration.count();

        std::vector<oc::block> receiver_shares;
        std::vector<oc::Channel> chls;
        chls.push_back(chl);
        receiver_shares = osn.run_osn(item_decrypt_masks, chls);

        //

        // for (int i = 0; i < num_cmps; i++)
        // {
        //   cout << i << ": " << static_cast<int>(res_shares[i]) << ": " << item_decrypt_masks[i] << ": " << receiver_shares[i] << "\n";
        // }

        //cout<<"start our work part-recovery!\n";
        // Allocate space for the received data
        std::vector<osuCrypto::block> rcv_ciphertexts(num_cmps);

        // Receive the actual data
        //chlfinal.recv(rcv_ciphertexts.data(), num_cmps * sizeof(osuCrypto::block));  //generate error
        chl.recv(rcv_ciphertexts);
        //cout<<"start our work part-finish receive!\n";

        for (int i = 0; i < num_cmps; i++)
        {
          osuCrypto::block plaintext;
          plaintext=rcv_ciphertexts[i] ^ receiver_shares[i];
          
          // uint64_t lower64Bits = *(reinterpret_cast<uint64_t *>(&plaintext));

          // // Optional: Convert to int if it fits
          // int intValue;
          // if (lower64Bits <= static_cast<uint64_t>(std::numeric_limits<int>::max()))
          // {
          //   intValue = static_cast<int>(lower64Bits);
          //   cout << i << ": " << intValue << "\n";
          // }
          // else
          // {
          //   std::cout << "Value is too large to fit into an int." << std::endl;
          // } 

        }

        //******************** ADD FOR OUR WORK ***********************//
      }
  
      const auto clock_time_cir_end = std::chrono::system_clock::now();
      const duration_millis cir_duration = clock_time_cir_end - clock_time_cir_start;
      context.timings.psm_time = cir_duration.count();
      const auto clock_time_total_end = std::chrono::system_clock::now();
      const duration_millis total_duration = clock_time_total_end - clock_time_total_start;
      context.timings.total = total_duration.count();
    }

    // // Writing resultant shares to file
    // cout << "Writing resultant shares to File ..." << endl;
    // ofstream res_file;
    // res_file.open("res_share_P" + to_string(context.role) + ".dat");
    // for (int i = 0; i < context.nbins; i++)
    // {
    //   res_file << res_shares[i] << endl;
    // }
    // res_file.close();
  }

  std::unique_ptr<CSocket> EstablishConnection(const std::string &address, uint16_t port,
                                               e_role role)
  {
    std::unique_ptr<CSocket> socket;
    if (role == SERVER)
    {
      socket = Listen(address.c_str(), port);
    }
    else
    {
      socket = Connect(address.c_str(), port);
    }
    assert(socket);
    return socket;
  }

  std::size_t PlainIntersectionSize(std::vector<std::uint64_t> v1, std::vector<std::uint64_t> v2)
  {
    std::vector<std::uint64_t> intersection_v;

    std::sort(v1.begin(), v1.end());
    std::sort(v2.begin(), v2.end());

    std::set_intersection(v1.begin(), v1.end(), v2.begin(), v2.end(), back_inserter(intersection_v));
    return intersection_v.size();
  }

  /*
   * Print Timings
   */
  void PrintTimings(const PsiAnalyticsContext &context)
  {
    std::cout << "Time for hashing " << context.timings.hashing << " ms\n";
    // std::cout << "Time for OPRF " << context.timings.oprf << " ms\n";
    // std::cout << "Time for hint computation " << context.timings.hint_computation << " ms\n";
    // std::cout << "Time for transmission of the hint "
    //           << context.timings.hint_transmission << " ms\n";
    // std::cout << "Timing for PSM " << context.timings.psm_time << " ms\n";
    std::cout << "Time for triples " << context.timings.triples << " ms\n";
    std::cout << "Time for baseOT_SCI " << context.timings.base_ots_sci << " ms\n";
    std::cout << "Time for OSN_setup " << context.timings.osn_setup << " ms\n";
    std::cout << "Time for baseOT_libOTe " <<context.timings.base_ots_libote << " ms\n";

    std::cout << "Total runtime: " << context.timings.total << "ms\n";
    std::cout << "Total runtime w/o base OTs: "
              << context.timings.total - context.timings.base_ots_sci -
                     context.timings.base_ots_libote-context.timings.osn_setup-context.timings.triples
              << "ms\n";
  }

  /*
   * Clear communication counts for new execution
   */
  void ResetCommunication(std::unique_ptr<CSocket> &sock, osuCrypto::Channel &chl, sci::NetIO *ioArr[2], PsiAnalyticsContext &context)
  {
    chl.resetStats();
    sock->ResetSndCnt();
    sock->ResetRcvCnt();
    context.sci_io_start.resize(2);
    for (int i = 0; i < 2; i++)
    {
      context.sci_io_start[i] = ioArr[i]->counter;
    }
  }

  /*
   * Measure communication
   */
  void AccumulateCommunicationPSI(std::unique_ptr<CSocket> &sock, osuCrypto::Channel &chl, sci::NetIO *ioArr[2], PsiAnalyticsContext &context)
  {

    context.sentBytesOPRF = chl.getTotalDataSent();
    context.recvBytesOPRF = chl.getTotalDataRecv();

    context.sentBytesHint = sock->getSndCnt();
    context.recvBytesHint = sock->getRcvCnt();

    context.sentBytesSCI = 0;
    context.recvBytesSCI = 0;

    for (int i = 0; i < 2; i++)
    {
      context.sentBytesSCI += ioArr[i]->counter - context.sci_io_start[i];
    }

    // Send SCI Communication
    if (context.role == CLIENT)
    {
      sock->Receive(&context.recvBytesSCI, sizeof(uint64_t));
      sock->Send(&context.sentBytesSCI, sizeof(uint64_t));
    }
    else
    {
      sock->Send(&context.sentBytesSCI, sizeof(uint64_t));
      sock->Receive(&context.recvBytesSCI, sizeof(uint64_t));
    }
  }

  /*
   * Print communication
   */
  void PrintCommunication(PsiAnalyticsContext &context)
  {
    context.sentBytes = context.sentBytesOPRF + context.sentBytesHint + context.sentBytesSCI;
    context.recvBytes = context.recvBytesOPRF + context.recvBytesHint + context.recvBytesSCI;
    std::cout << context.role << ": Communication Statistics: " << std::endl;
    double sentinMB, recvinMB;
    sentinMB = context.sentBytesOPRF / ((1.0 * (1ULL << 20)));
    recvinMB = context.recvBytesOPRF / ((1.0 * (1ULL << 20)));
    std::cout << context.role << ": Sent Data OPRF (MB): " << sentinMB << std::endl;
    std::cout << context.role << ": Received Data OPRF (MB): " << recvinMB << std::endl;

    sentinMB = context.sentBytesHint / ((1.0 * (1ULL << 20)));
    recvinMB = context.recvBytesHint / ((1.0 * (1ULL << 20)));
    std::cout << context.role << ": Sent Data Hint (MB): " << sentinMB << std::endl;
    std::cout << context.role << ": Received Data Hint (MB): " << recvinMB << std::endl;

    sentinMB = context.sentBytesSCI / ((1.0 * (1ULL << 20)));
    recvinMB = context.recvBytesSCI / ((1.0 * (1ULL << 20)));
    std::cout << context.role << ": Sent Data CryptFlow2 (MB): " << sentinMB << std::endl;
    std::cout << context.role << ": Received Data CryptFlow2 (MB): " << recvinMB << std::endl;

    sentinMB = context.sentBytes / ((1.0 * (1ULL << 20)));
    recvinMB = context.recvBytes / ((1.0 * (1ULL << 20)));
    std::cout << context.role << ": Total Sent Data (MB): " << sentinMB << std::endl;
    std::cout << context.role << ": Total Received Data (MB): " << recvinMB << std::endl;
    std::cout << context.role << ": Total Data (MB): " << sentinMB+ recvinMB << std::endl;
  }

}
