/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Client.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "Client.h"

#include <chrono>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/math/distributions/normal.hpp>
#if ETH_JSONRPC || !ETH_TRUE
#include <jsonrpccpp/client.h>
#include <jsonrpccpp/client/connectors/httpclient.h>
#endif
#include <libdevcore/Log.h>
#include <libdevcore/StructuredLogger.h>
#include <libp2p/Host.h>
#if ETH_JSONRPC || !ETH_TRUE
#include "Sentinel.h"
#endif
#include "Defaults.h"
#include "Executive.h"
#include "EthereumHost.h"
using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace p2p;

VersionChecker::VersionChecker(string const& _dbPath):
	m_path(_dbPath.size() ? _dbPath : Defaults::dbPath())
{
	bytes statusBytes = contents(m_path + "/status");
	RLP status(statusBytes);
	try
	{
		auto protocolVersion = (unsigned)status[0];
		(void)protocolVersion;
		auto minorProtocolVersion = (unsigned)status[1];
		auto databaseVersion = (unsigned)status[2];
		h256 ourGenesisHash = CanonBlockChain::genesis().hash();
		auto genesisHash = status.itemCount() > 3 ? (h256)status[3] : ourGenesisHash;

		m_action =
			databaseVersion != c_databaseVersion || genesisHash != ourGenesisHash ?
				WithExisting::Kill
			: minorProtocolVersion != eth::c_minorProtocolVersion ?
				WithExisting::Verify
			:
				WithExisting::Trust;
	}
	catch (...)
	{
		m_action = WithExisting::Kill;
	}
}

void VersionChecker::setOk()
{
	if (m_action != WithExisting::Trust)
	{
		try
		{
			boost::filesystem::create_directory(m_path);
		}
		catch (...)
		{
			cwarn << "Unhandled exception! Failed to create directory: " << m_path << "\n" << boost::current_exception_diagnostic_information();
		}
		writeFile(m_path + "/status", rlpList(eth::c_protocolVersion, eth::c_minorProtocolVersion, c_databaseVersion, CanonBlockChain::genesis().hash()));
	}
}

void Client::onBadBlock(Exception& _ex) const
{
	// BAD BLOCK!!!
	bytes const* block = boost::get_error_info<errinfo_block>(_ex);
	if (!block)
	{
		cwarn << "ODD: onBadBlock called but exception has no block in it.";
		return;
	}

	badBlock(*block, _ex.what());

#if ETH_JSONRPC || !ETH_TRUE
	Json::Value report;

	report["client"] = "cpp";
	report["version"] = Version;
	report["protocolVersion"] = c_protocolVersion;
	report["databaseVersion"] = c_databaseVersion;
	report["errortype"] = _ex.what();
	report["block"] = toHex(*block);

	// add the various hints.
	if (unsigned const* uncleIndex = boost::get_error_info<errinfo_uncleIndex>(_ex))
	{
		// uncle that failed.
		report["hints"]["uncleIndex"] = *uncleIndex;
	}
	else if (unsigned const* txIndex = boost::get_error_info<errinfo_transactionIndex>(_ex))
	{
		// transaction that failed.
		report["hints"]["transactionIndex"] = *txIndex;
	}
	else
	{
		// general block failure.
	}

	if (string const* vmtraceJson = boost::get_error_info<errinfo_vmtrace>(_ex))
		Json::Reader().parse(*vmtraceJson, report["hints"]["vmtrace"]);

	if (vector<bytes> const* receipts = boost::get_error_info<errinfo_receipts>(_ex))
	{
		report["hints"]["receipts"] = Json::arrayValue;
		for (auto const& r: *receipts)
			report["hints"]["receipts"].append(toHex(r));
	}
	if (h256Hash const* excluded = boost::get_error_info<errinfo_unclesExcluded>(_ex))
	{
		report["hints"]["unclesExcluded"] = Json::arrayValue;
		for (auto const& r: h256Set() + *excluded)
			report["hints"]["unclesExcluded"].append(Json::Value(r.hex()));
	}

#define DEV_HINT_ERRINFO(X) \
		if (auto const* n = boost::get_error_info<errinfo_ ## X>(_ex)) \
			report["hints"][#X] = toString(*n)
#define DEV_HINT_ERRINFO_HASH(X) \
		if (auto const* n = boost::get_error_info<errinfo_ ## X>(_ex)) \
			report["hints"][#X] = n->hex()

	DEV_HINT_ERRINFO_HASH(hash256);
	DEV_HINT_ERRINFO(uncleNumber);
	DEV_HINT_ERRINFO(currentNumber);
	DEV_HINT_ERRINFO(now);
	DEV_HINT_ERRINFO(invalidSymbol);
	DEV_HINT_ERRINFO(wrongAddress);
	DEV_HINT_ERRINFO(comment);
	DEV_HINT_ERRINFO(min);
	DEV_HINT_ERRINFO(max);
	DEV_HINT_ERRINFO(name);
	DEV_HINT_ERRINFO(field);
	DEV_HINT_ERRINFO(data);
	DEV_HINT_ERRINFO_HASH(nonce);
	DEV_HINT_ERRINFO(difficulty);
	DEV_HINT_ERRINFO(target);
	DEV_HINT_ERRINFO_HASH(seedHash);
	DEV_HINT_ERRINFO_HASH(mixHash);
	if (tuple<h256, h256> const* r = boost::get_error_info<errinfo_ethashResult>(_ex))
	{
		report["hints"]["ethashResult"]["value"] = get<0>(*r).hex();
		report["hints"]["ethashResult"]["mixHash"] = get<1>(*r).hex();
	}
	DEV_HINT_ERRINFO(required);
	DEV_HINT_ERRINFO(got);
	DEV_HINT_ERRINFO_HASH(required_LogBloom);
	DEV_HINT_ERRINFO_HASH(got_LogBloom);
	DEV_HINT_ERRINFO_HASH(required_h256);
	DEV_HINT_ERRINFO_HASH(got_h256);

	cwarn << ("Report: \n" + Json::StyledWriter().write(report));

	if (!m_sentinel.empty())
	{
		jsonrpc::HttpClient client(m_sentinel);
		Sentinel rpc(client);
		try
		{
			rpc.eth_badBlock(report);
		}
		catch (...)
		{
			cwarn << "Error reporting to sentinel. Sure the address" << m_sentinel << "is correct?";
		}
	}
#endif
}

void BasicGasPricer::update(BlockChain const& _bc)
{
	unsigned c = 0;
	h256 p = _bc.currentHash();
	m_gasPerBlock = _bc.info(p).gasLimit;

	map<u256, u256> dist;
	u256 total = 0;

	// make gasPrice versus gasUsed distribution for the last 1000 blocks
	while (c < 1000 && p)
	{
		BlockInfo bi = _bc.info(p);
		if (bi.transactionsRoot != EmptyTrie)
		{
			auto bb = _bc.block(p);
			RLP r(bb);
			BlockReceipts brs(_bc.receipts(bi.hash()));
			size_t i = 0;
			for (auto const& tr: r[1])
			{
				Transaction tx(tr.data(), CheckTransaction::None);
				u256 gu = brs.receipts[i].gasUsed();
				dist[tx.gasPrice()] += gu;
				total += gu;
				i++;
			}
		}
		p = bi.parentHash;
		++c;
	}

	// fill m_octiles with weighted gasPrices
	if (total > 0)
	{
		m_octiles[0] = dist.begin()->first;

		// calc mean
		u256 mean = 0;
		for (auto const& i: dist)
			mean += i.first * i.second;
		mean /= total;

		// calc standard deviation
		u256 sdSquared = 0;
		for (auto const& i: dist)
			sdSquared += i.second * (i.first - mean) * (i.first - mean);
		sdSquared /= total;

		if (sdSquared)
		{
			long double sd = sqrt(sdSquared.convert_to<long double>());
			long double normalizedSd = sd / mean.convert_to<long double>();

			// calc octiles normalized to gaussian distribution
			boost::math::normal gauss(1.0, (normalizedSd > 0.01) ? normalizedSd : 0.01);
			for (size_t i = 1; i < 8; i++)
				m_octiles[i] = u256(mean.convert_to<long double>() * boost::math::quantile(gauss, i / 8.0));
			m_octiles[8] = dist.rbegin()->first;
		}
		else
		{
			for (size_t i = 0; i < 9; i++)
				m_octiles[i] = (i + 1) * mean / 5;
		}
	}
}

std::ostream& dev::eth::operator<<(std::ostream& _out, ActivityReport const& _r)
{
	_out << "Since " << toString(_r.since) << " (" << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - _r.since).count();
	_out << "): " << _r.ticks << "ticks";
	return _out;
}

#ifdef _WIN32
const char* ClientNote::name() { return EthTeal "^" EthBlue " i"; }
const char* ClientChat::name() { return EthTeal "^" EthWhite " o"; }
const char* ClientTrace::name() { return EthTeal "^" EthGray " O"; }
const char* ClientDetail::name() { return EthTeal "^" EthCoal " 0"; }
#else
const char* ClientNote::name() { return EthTeal "⧫" EthBlue " ℹ"; }
const char* ClientChat::name() { return EthTeal "⧫" EthWhite " ◌"; }
const char* ClientTrace::name() { return EthTeal "⧫" EthGray " ◎"; }
const char* ClientDetail::name() { return EthTeal "⧫" EthCoal " ●"; }
#endif

Client::Client(p2p::Host* _extNet, std::string const& _dbPath, WithExisting _forceAction, u256 _networkId):
	Client(_extNet, make_shared<TrivialGasPricer>(), _dbPath, _forceAction, _networkId)
{
	startWorking();
}

Client::Client(p2p::Host* _extNet, std::shared_ptr<GasPricer> _gp, std::string const& _dbPath, WithExisting _forceAction, u256 _networkId):
	Worker("eth", 0),
	m_vc(_dbPath),
	m_bc(_dbPath, max(m_vc.action(), _forceAction), [](unsigned d, unsigned t){ cerr << "REVISING BLOCKCHAIN: Processed " << d << " of " << t << "...\r"; }),
	m_gp(_gp),
	m_stateDB(State::openDB(_dbPath, max(m_vc.action(), _forceAction))),
	m_preMine(m_stateDB, BaseState::CanonGenesis),
	m_postMine(m_stateDB)
{
	m_lastGetWork = std::chrono::system_clock::now() - chrono::seconds(30);
	m_tqReady = m_tq.onReady([=](){ this->onTransactionQueueReady(); });	// TODO: should read m_tq->onReady(thisThread, syncTransactionQueue);
	m_bqReady = m_bq.onReady([=](){ this->onBlockQueueReady(); });			// TODO: should read m_bq->onReady(thisThread, syncBlockQueue);
	m_bq.setOnBad([=](Exception& ex){ this->onBadBlock(ex); });
	m_bc.setOnBad([=](Exception& ex){ this->onBadBlock(ex); });
	m_farm.onSolutionFound([=](ProofOfWork::Solution const& s){ return this->submitWork(s); });

	m_gp->update(m_bc);

	auto host = _extNet->registerCapability(new EthereumHost(m_bc, m_tq, m_bq, _networkId));
	m_host = host;
	_extNet->addCapability(host, EthereumHost::staticName(), EthereumHost::c_oldProtocolVersion); //TODO: remove this one v61+ protocol is common

	if (_dbPath.size())
		Defaults::setDBPath(_dbPath);
	m_vc.setOk();
	doWork();

	startWorking();
}

Client::~Client()
{
	stopWorking();
}

static const Address c_canary("0x");

bool Client::isChainBad() const
{
	return stateAt(c_canary, 0) != 0;
}

bool Client::isUpgradeNeeded() const
{
	return stateAt(c_canary, 0) == 2;
}

void Client::setNetworkId(u256 _n)
{
	if (auto h = m_host.lock())
		h->setNetworkId(_n);
}

DownloadMan const* Client::downloadMan() const
{
	if (auto h = m_host.lock())
		return &(h->downloadMan());
	return nullptr;
}

bool Client::isSyncing() const
{
	if (auto h = m_host.lock())
		return h->isSyncing();
	return false;
}

void Client::startedWorking()
{
	// Synchronise the state according to the head of the block chain.
	// TODO: currently it contains keys for *all* blocks. Make it remove old ones.
	cdebug << "startedWorking()";

	DEV_WRITE_GUARDED(x_preMine)
		m_preMine.sync(m_bc);
	DEV_READ_GUARDED(x_preMine)
	{
		DEV_WRITE_GUARDED(x_working)
			m_working = m_preMine;
		DEV_WRITE_GUARDED(x_postMine)
			m_postMine = m_preMine;
	}
}

void Client::doneWorking()
{
	// Synchronise the state according to the head of the block chain.
	// TODO: currently it contains keys for *all* blocks. Make it remove old ones.
	DEV_WRITE_GUARDED(x_preMine)
		m_preMine.sync(m_bc);
	DEV_READ_GUARDED(x_preMine)
	{
		DEV_WRITE_GUARDED(x_working)
			m_working = m_preMine;
		DEV_WRITE_GUARDED(x_postMine)
			m_postMine = m_preMine;
	}
}

void Client::killChain()
{
	bool wasMining = isMining();
	if (wasMining)
		stopMining();
	stopWorking();

	m_tq.clear();
	m_bq.clear();
	m_farm.stop();

	{
		WriteGuard l(x_postMine);
		WriteGuard l2(x_preMine);
		WriteGuard l3(x_working);

		m_preMine = State();
		m_postMine = State();
		m_working = State();

		m_stateDB = OverlayDB();
		m_stateDB = State::openDB(Defaults::dbPath(), WithExisting::Kill);
		m_bc.reopen(Defaults::dbPath(), WithExisting::Kill);

		m_preMine = State(m_stateDB, BaseState::CanonGenesis);
		m_postMine = State(m_stateDB);
	}

	if (auto h = m_host.lock())
		h->reset();

	startedWorking();
	doWork();

	startWorking();
	if (wasMining)
		startMining();
}

void Client::clearPending()
{
	DEV_WRITE_GUARDED(x_postMine)
	{
		if (!m_postMine.pending().size())
			return;
		m_tq.clear();
		DEV_READ_GUARDED(x_preMine)
			m_postMine = m_preMine;
	}

	startMining();
	h256Hash changeds;
	noteChanged(changeds);
}

template <class S, class T>
static S& filtersStreamOut(S& _out, T const& _fs)
{
	_out << "{";
	unsigned i = 0;
	for (h256 const& f: _fs)
	{
		_out << (i++ ? ", " : "");
		if (f == PendingChangedFilter)
			_out << LogTag::Special << "pending";
		else if (f == ChainChangedFilter)
			_out << LogTag::Special << "chain";
		else
			_out << f;
	}
	_out << "}";
	return _out;
}

void Client::appendFromNewPending(TransactionReceipt const& _receipt, h256Hash& io_changed, h256 _sha3)
{
	Guard l(x_filtersWatches);
	io_changed.insert(PendingChangedFilter);
	m_specialFilters.at(PendingChangedFilter).push_back(_sha3);
	for (pair<h256 const, InstalledFilter>& i: m_filters)
	{
		// acceptable number.
		auto m = i.second.filter.matches(_receipt);
		if (m.size())
		{
			// filter catches them
			for (LogEntry const& l: m)
				i.second.changes.push_back(LocalisedLogEntry(l));
			io_changed.insert(i.first);
		}
	}
}

void Client::appendFromNewBlock(h256 const& _block, h256Hash& io_changed)
{
	// TODO: more precise check on whether the txs match.
	auto d = m_bc.info(_block);
	auto receipts = m_bc.receipts(_block).receipts;

	Guard l(x_filtersWatches);
	io_changed.insert(ChainChangedFilter);
	m_specialFilters.at(ChainChangedFilter).push_back(_block);
	for (pair<h256 const, InstalledFilter>& i: m_filters)
	{
		// acceptable number & looks like block may contain a matching log entry.
		unsigned logIndex = 0;
		for (size_t j = 0; j < receipts.size(); j++)
		{
			logIndex++;
			auto tr = receipts[j];
			auto m = i.second.filter.matches(tr);
			if (m.size())
			{
				auto transactionHash = transaction(d.hash(), j).sha3();
				// filter catches them
				for (LogEntry const& l: m)
					i.second.changes.push_back(LocalisedLogEntry(l, d, transactionHash, j, logIndex));
				io_changed.insert(i.first);
			}
		}
	}
}

void Client::setForceMining(bool _enable)
{
	 m_forceMining = _enable;
	 if (isMining())
		startMining();
}

MiningProgress Client::miningProgress() const
{
	if (m_farm.isMining())
		return m_farm.miningProgress();
	return MiningProgress();
}

uint64_t Client::hashrate() const
{
	if (m_farm.isMining())
		return m_farm.miningProgress().rate();
	return 0;
}

std::list<MineInfo> Client::miningHistory()
{
	std::list<MineInfo> ret;
/*	ReadGuard l(x_localMiners);
	if (m_localMiners.empty())
		return ret;
	ret = m_localMiners[0].miningHistory();
	for (unsigned i = 1; i < m_localMiners.size(); ++i)
	{
		auto l = m_localMiners[i].miningHistory();
		auto ri = ret.begin();
		auto li = l.begin();
		for (; ri != ret.end() && li != l.end(); ++ri, ++li)
			ri->combine(*li);
	}*/
	return ret;
}

ExecutionResult Client::call(Address _dest, bytes const& _data, u256 _gas, u256 _value, u256 _gasPrice, Address const& _from)
{
	ExecutionResult ret;
	try
	{
		State temp;
//		cdebug << "Nonce at " << toAddress(_secret) << " pre:" << m_preMine.transactionsFrom(toAddress(_secret)) << " post:" << m_postMine.transactionsFrom(toAddress(_secret));
		DEV_READ_GUARDED(x_postMine)
			temp = m_postMine;
		temp.addBalance(_from, _value + _gasPrice * _gas);
		Executive e(temp, LastHashes(), 0);
		e.setResultRecipient(ret);
		if (!e.call(_dest, _from, _value, _gasPrice, &_data, _gas))
			e.go();
		e.finalize();
	}
	catch (...)
	{
		// TODO: Some sort of notification of failure.
	}
	return ret;
}

ProofOfWork::WorkPackage Client::getWork()
{
	// lock the work so a later submission isn't invalidated by processing a transaction elsewhere.
	// this will be reset as soon as a new block arrives, allowing more transactions to be processed.
	bool oldShould = shouldServeWork();
	m_lastGetWork = chrono::system_clock::now();

	if (!m_mineOnBadChain && isChainBad())
		return ProofOfWork::WorkPackage();

	// if this request has made us bother to serve work, prep it now.
	if (!oldShould && shouldServeWork())
		onPostStateChanged();
	else
		// otherwise, set this to true so that it gets prepped next time.
		m_remoteWorking = true;
	return ProofOfWork::package(m_miningInfo);
}

bool Client::submitWork(ProofOfWork::Solution const& _solution)
{
	bytes newBlock;
	DEV_WRITE_GUARDED(x_working)
		if (!m_working.completeMine<ProofOfWork>(_solution))
			return false;

	DEV_READ_GUARDED(x_working)
	{
		DEV_WRITE_GUARDED(x_postMine)
			m_postMine = m_working;
		newBlock = m_working.blockData();
	}

	// OPTIMISE: very inefficient to not utilise the existing OverlayDB in m_postMine that contains all trie changes.
	m_bq.import(&newBlock, m_bc, true);

	return true;
}

unsigned static const c_syncMin = 1;
unsigned static const c_syncMax = 100;
double static const c_targetDuration = 1;

void Client::syncBlockQueue()
{
	ImportRoute ir;
	cwork << "BQ ==> CHAIN ==> STATE";
	boost::timer t;
	tie(ir.first, ir.second, m_syncBlockQueue) = m_bc.sync(m_bq, m_stateDB, m_syncAmount);
	double elapsed = t.elapsed();

	cnote << m_syncAmount << "blocks imported in" << unsigned(elapsed * 1000) << "ms (" << (m_syncAmount / elapsed) << "blocks/s)";

	if (elapsed > c_targetDuration * 1.1 && m_syncAmount > c_syncMin)
		m_syncAmount = max(c_syncMin, m_syncAmount * 9 / 10);
	else if (elapsed < c_targetDuration * 0.9 && m_syncAmount < c_syncMax)
		m_syncAmount = min(c_syncMax, m_syncAmount * 11 / 10 + 1);
	if (ir.first.empty())
		return;
	onChainChanged(ir);
}

void Client::syncTransactionQueue()
{
	// returns TransactionReceipts, once for each transaction.
	cwork << "postSTATE <== TQ";

	h256Hash changeds;
	TransactionReceipts newPendingReceipts;

	DEV_WRITE_GUARDED(x_working)
		tie(newPendingReceipts, m_syncTransactionQueue) = m_working.sync(m_bc, m_tq, *m_gp);

	if (newPendingReceipts.empty())
		return;

	DEV_READ_GUARDED(x_working)
		DEV_WRITE_GUARDED(x_postMine)
			m_postMine = m_working;

	DEV_READ_GUARDED(x_postMine)
		for (size_t i = 0; i < newPendingReceipts.size(); i++)
			appendFromNewPending(newPendingReceipts[i], changeds, m_postMine.pending()[i].sha3());


	// Tell farm about new transaction (i.e. restartProofOfWork mining).
	onPostStateChanged();

	// Tell watches about the new transactions.
	noteChanged(changeds);

	// Tell network about the new transactions.
	if (auto h = m_host.lock())
		h->noteNewTransactions();
}

void Client::onChainChanged(ImportRoute const& _ir)
{
	// insert transactions that we are declaring the dead part of the chain
	for (auto const& h: _ir.second)
	{
		clog(ClientNote) << "Dead block:" << h;
		for (auto const& t: m_bc.transactions(h))
		{
			clog(ClientNote) << "Resubmitting dead-block transaction " << Transaction(t, CheckTransaction::None);
			m_tq.import(t, TransactionQueue::ImportCallback(), IfDropped::Retry);
		}
	}

	// remove transactions from m_tq nicely rather than relying on out of date nonce later on.
	for (auto const& h: _ir.first)
	{
		clog(ClientChat) << "Live block:" << h;
		for (auto const& th: m_bc.transactionHashes(h))
		{
			clog(ClientNote) << "Safely dropping transaction " << th;
			m_tq.drop(th);
		}
	}

	if (auto h = m_host.lock())
		h->noteNewBlocks();

	h256Hash changeds;
	for (auto const& h: _ir.first)
		appendFromNewBlock(h, changeds);

	// RESTART MINING

	if (!m_bq.items().first)
	{
		bool preChanged = false;
		State newPreMine;
		DEV_READ_GUARDED(x_preMine)
			newPreMine = m_preMine;

		// TODO: use m_postMine to avoid re-evaluating our own blocks.
		preChanged = newPreMine.sync(m_bc);

		if (preChanged || m_postMine.address() != m_preMine.address())
		{
			if (isMining())
				cnote << "New block on chain.";

			DEV_WRITE_GUARDED(x_preMine)
				m_preMine = newPreMine;
			DEV_WRITE_GUARDED(x_working)
				m_working = newPreMine;
			DEV_READ_GUARDED(x_postMine)
				for (auto const& t: m_postMine.pending())
				{
					clog(ClientNote) << "Resubmitting post-mine transaction " << t;
					auto ir = m_tq.import(t, TransactionQueue::ImportCallback(), IfDropped::Retry);
					if (ir != ImportResult::Success)
						onTransactionQueueReady();
				}
			DEV_READ_GUARDED(x_working) DEV_WRITE_GUARDED(x_postMine)
				m_postMine = m_working;

			changeds.insert(PendingChangedFilter);

			onPostStateChanged();
		}

		// Quick hack for now - the TQ at this point already has the prior pending transactions in it;
		// we should resync with it manually until we are stricter about what constitutes "knowing".
		onTransactionQueueReady();
	}

	noteChanged(changeds);
}

bool Client::remoteActive() const
{
	return chrono::system_clock::now() - m_lastGetWork < chrono::seconds(30);
}

void Client::onPostStateChanged()
{
	cnote << "Post state changed.";
	rejigMining();
	m_remoteWorking = false;
}

void Client::startMining()
{
	m_wouldMine = true;
	rejigMining();
}

void Client::rejigMining()
{
	if ((wouldMine() || remoteActive()) && !m_bq.items().first && (!isChainBad() || mineOnBadChain()) /*&& (forceMining() || transactionsWaiting())*/)
	{
		cnote << "Rejigging mining...";
		DEV_WRITE_GUARDED(x_working)
			m_working.commitToMine(m_bc);
		DEV_READ_GUARDED(x_working)
		{
			DEV_WRITE_GUARDED(x_postMine)
				m_postMine = m_working;
			m_miningInfo = m_postMine.info();
		}

		if (m_wouldMine)
		{
			m_farm.setWork(m_miningInfo);
			if (m_turboMining)
				m_farm.startGPU();
			else
				m_farm.startCPU();

			m_farm.setWork(m_miningInfo);
			Ethash::ensurePrecomputed(m_bc.number());
		}
	}
	if (!m_wouldMine)
		m_farm.stop();
}

void Client::noteChanged(h256Hash const& _filters)
{
	Guard l(x_filtersWatches);
	if (_filters.size())
		filtersStreamOut(cwatch << "noteChanged:", _filters);
	// accrue all changes left in each filter into the watches.
	for (auto& w: m_watches)
		if (_filters.count(w.second.id))
		{
			if (m_filters.count(w.second.id))
			{
				cwatch << "!!!" << w.first << w.second.id.abridged();
				w.second.changes += m_filters.at(w.second.id).changes;
			}
			else if (m_specialFilters.count(w.second.id))
				for (h256 const& hash: m_specialFilters.at(w.second.id))
				{
					cwatch << "!!!" << w.first << LogTag::Special << (w.second.id == PendingChangedFilter ? "pending" : w.second.id == ChainChangedFilter ? "chain" : "???");
					w.second.changes.push_back(LocalisedLogEntry(SpecialLogEntry, hash));
				}
		}
	// clear the filters now.
	for (auto& i: m_filters)
		i.second.changes.clear();
	for (auto& i: m_specialFilters)
		i.second.clear();
}

void Client::doWork()
{
	bool t = true;
	if (m_syncBlockQueue.compare_exchange_strong(t, false))
		syncBlockQueue();

	t = true;
	if (m_syncTransactionQueue.compare_exchange_strong(t, false) && !m_remoteWorking && !isSyncing())
		syncTransactionQueue();

	tick();

	if (!m_syncBlockQueue && !m_syncTransactionQueue)
	{
		std::unique_lock<std::mutex> l(x_signalled);
		m_signalled.wait_for(l, chrono::seconds(1));
	}
}

void Client::tick()
{
	if (chrono::system_clock::now() - m_lastTick > chrono::seconds(1))
	{
		m_report.ticks++;
		checkWatchGarbage();
		m_bq.tick(m_bc);
		m_lastTick = chrono::system_clock::now();
		if (m_report.ticks == 15)
			clog(ClientTrace) << activityReport();
	}
}

void Client::checkWatchGarbage()
{
	if (chrono::system_clock::now() - m_lastGarbageCollection > chrono::seconds(5))
	{
		// watches garbage collection
		vector<unsigned> toUninstall;
		DEV_GUARDED(x_filtersWatches)
			for (auto key: keysOf(m_watches))
				if (m_watches[key].lastPoll != chrono::system_clock::time_point::max() && chrono::system_clock::now() - m_watches[key].lastPoll > chrono::seconds(20))
				{
					toUninstall.push_back(key);
					cnote << "GC: Uninstall" << key << "(" << chrono::duration_cast<chrono::seconds>(chrono::system_clock::now() - m_watches[key].lastPoll).count() << "s old)";
				}
		for (auto i: toUninstall)
			uninstallWatch(i);

		// blockchain GC
		m_bc.garbageCollect();

		m_lastGarbageCollection = chrono::system_clock::now();
	}
}

State Client::asOf(h256 const& _block) const
{
	try
	{
		State ret(m_stateDB);
		ret.populateFromChain(bc(), _block);
		return ret;
	}
	catch (Exception& ex)
	{
		ex << errinfo_block(bc().block(_block));
		onBadBlock(ex);
		return State();
	}
}

void Client::prepareForTransaction()
{
	startWorking();
}

State Client::state(unsigned _txi, h256 _block) const
{
	try
	{
		State ret(m_stateDB);
		ret.populateFromChain(m_bc, _block);
		return ret.fromPending(_txi);
	}
	catch (Exception& ex)
	{
		ex << errinfo_block(bc().block(_block));
		onBadBlock(ex);
		return State();
	}
}

State Client::state(h256 const& _block, PopulationStatistics* o_stats) const
{
	try
	{
		State ret(m_stateDB);
		PopulationStatistics s = ret.populateFromChain(m_bc, _block);
		if (o_stats)
			swap(s, *o_stats);
		return ret;
	}
	catch (Exception& ex)
	{
		ex << errinfo_block(bc().block(_block));
		onBadBlock(ex);
		return State();
	}
}

eth::State Client::state(unsigned _txi) const
{
	DEV_READ_GUARDED(x_postMine)
		return m_postMine.fromPending(_txi);
	assert(false);
	return State();
}

void Client::flushTransactions()
{
	doWork();
}

SyncStatus Client::syncStatus() const
{
	auto h = m_host.lock();
	return h ? h->status() : SyncStatus();
}
