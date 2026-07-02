// Copyright (c) 2025 The DigiByte Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/ddcoincontrol.h>

namespace wallet {

DDCoinControl::DDCoinControl()
{
    m_selected_inputs.clear();
}

bool DDCoinControl::HasSelected() const
{
    return !m_selected_inputs.empty();
}

bool DDCoinControl::IsSelected(const COutPoint& output) const
{
    return m_selected_inputs.count(output) > 0;
}

void DDCoinControl::Select(const COutPoint& output)
{
    m_selected_inputs.insert(output);
}

void DDCoinControl::UnSelect(const COutPoint& output)
{
    m_selected_inputs.erase(output);
}

void DDCoinControl::UnSelectAll()
{
    m_selected_inputs.clear();
}

std::vector<COutPoint> DDCoinControl::ListSelected() const
{
    return std::vector<COutPoint>(m_selected_inputs.begin(), m_selected_inputs.end());
}

} // namespace wallet
