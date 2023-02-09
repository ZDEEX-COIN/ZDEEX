// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zsigndialog.h"
#include "ui_zsigndialog.h"
#include "init.h"     //for *pwalletMain
#include "core_io.h"  //for EncodeHexTx()
#include "util.h"     //for HexToCharArray()
#include "rpc/client.h" //for RPCConvertValues()
#include "rpc/server.h" //CRPCTable::execute


#include "addresstablemodel.h"
#include "komodounits.h"
#include "clientmodel.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"

#include "base58.h"
#include "chainparams.h"
#include "coincontrol.h"
#include "ui_interface.h"
#include "txmempool.h"
#include "main.h"
#include "policy/fees.h"
#include "wallet/wallet_fees.h"
#include "wallet/wallet.h"
#include "key_io.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "zaddresstablemodel.h"

#include <QFontMetrics>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTimer>

extern CRPCTable tableRPC;

ZSignDialog::ZSignDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ZSignDialog),
    clientModel(0),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons())
    {
        ui->clearButton->setIcon(QIcon());
        ui->signButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->signButton->setIcon(_platformStyle->SingleColorIcon(":/icons/send"));
    }

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->signButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));

    connect(ui->teInput, SIGNAL(textChanged()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->teResult, SIGNAL(textChanged()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->teInput, SIGNAL(cursorPositionChanged()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->teResult, SIGNAL(cursorPositionChanged()), this, SLOT(sendResetUnlockSignal()));

    //Reset the GUI
    ui->teInput->clear();
    ui->lbResult->setText("Signed transaction output:");
    ui->teResult->clear();
}

ZSignDialog::~ZSignDialog()
{
    delete ui;
}

void ZSignDialog::sendResetUnlockSignal() {
    Q_EMIT resetUnlockTimerEvent();
}

void ZSignDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

void ZSignDialog::setModel(WalletModel *_model)
{
    this->model = _model;
}

void ZSignDialog::on_signButton_clicked()
{
    UniValue oResult;
    QString sMsg;

    try
    {
      ui->lbResult->setText("Signed transaction output:");

      QString sTransaction = ui->teInput->toPlainText();
      sTransaction = sTransaction.trimmed();
      //When creating the transaction from the debug console, the result is wrapped
      //in "quotation" marks, with the quotations inside the message escaped (\")
      //Replace the escaped quotations with normal quotation character:
      if ( sTransaction.contains("\\\"") )
      {
        sTransaction = sTransaction.replace("\\\"","\"");
      }
      QStringList sTransactionParts = sTransaction.split(" ");

      //No text provided yet:
      if (sTransactionParts.size() == 0)
      {
        return;
      }

      UniValue params(UniValue::VARR);
      std::vector<std::string> strParams;

      QString qsResult="";

      
      bool bParamSizeInvalid=true;
      if (sTransactionParts.size()>=17) //Minimum transaction size
      {
        if(sTransactionParts[2] == '1')
        {
          if (sTransactionParts.size() == 17)
          {
            bParamSizeInvalid=false;
          }
        }        
      }
      
      if (
         (sTransactionParts[0] != "z_sign_offline") ||
         (bParamSizeInvalid==true)
         )
      {
        qsResult = "z_sign_offline signs the transaction presented in the hex input block\n"
          "The transaction data block is generated by an online wallet using the 'z_sendmany_prepare_offline' command\n"
          "\nArguments:\n"
          "0. Project                 (string, required) Project: arrr=Pirate Chain\n"
          "1. Version                 (number) The version of the transaction structure. Currently only '1' supported\n"
          "2. \"from_address\"        (string, required) The taddr or zaddr to send the funds from.\n"
          "3. \"spending notes\"      (array, required) An array of json objects representing the spending notes of the from_address.\n"
          "    '[{\n"
          "      \"witnessposition\":position (numeric, required) spending witness blockchain position\n"
          "      \"witnesspath\"    :path     (hex string, required) spending witness blockchain path, 2 chars/hex value\n"
          "      \"note_d\"         :d        (hex string, required) Note d component, 2 chars/hex value\n"
          "      \"note_pkd\"       :pkd      (hex string, required) Note pkd component, 2 chars/hex value\n"
          "      \"note_r\"         :r        (hex string, required) Note r component, 2 chars/hex value\n"
          "      \"value\"          :value    (numeric, required) amount stored in the note\n"
          "      \"zip212\"         :value    (numeric, required) zip212 status of the note: 0=BeforeZip212, 1=AfterZip212\n"
          "    }, ... ]'\n"
          "4. \"outputs\"             (array, required) An array of json objects representing recipients and the amounts send to them.\n"
          "    '[{\n"
          "      \"address\":address  (string, required) The address is a zaddr\n"
          "      \"amount\":amount    (numeric, required) The numeric amount in KMD is the value\n"
          "      \"memo\":memo        (hex string, optional) A note about the payment, 2 chars/hex value\n"
          "    }, ... ]'\n"
          "5. Minconf                 (numeric, required) Only use funds confirmed at least this many times.\n"
          "6. Fee                     (numeric, required"
          "7. Next block height       (numeric, required) Network next block height\n"
          "8. branch ID               (numeric, required) Network branch ID\n"
          "9. \"anchor\"                (hex string, required) Anchor for the witnesses\n"
          "10. MTX overwintered        (numeric, required) Transaction: Overwintered\n"
          "11.MTX ExpiryHeight        (numeric, required) Transaction: ExpiryHeight\n"
          "12.MTX VersionGroupID      (numeric, required) Transaction: VersionGroupID\n"
          "13.MTX Version             (numeric, required) Transaction: Version\n"
          "14.ZIP212 enabled          (numeric, required) For outputs: 0=BeforeZip212, 1=AfterZip212\n"          
          "15.Checksum                (numeric, required) Sum of the ASCII values of all the characters in this command\n"
          "Result:\n"\
          "\"sendrawtransaction\"     (string) A string containing the transaction data that must be pasted into the online wallet\n"
          "                           to complete the transaction";

          ui->teResult->setText(qsResult);
          return;
      }

      //Convert the QString array to vector< std::string >
      QString sTmp;
      for (int iI=1;iI<sTransactionParts.size();iI++)
      {
        //Is it an array element?
        if (sTransactionParts[iI][0]=="'")
        {
          //Array doesn't have the 'single' quotes anymore,
          //but internally, the strings are still enclosed
          //in "double quotes".
          sTmp = sTransactionParts[iI].replace("'","");
          strParams.push_back ( sTmp.toStdString() );
        }
        else
        {
          //If it's not an array, then the "double quotes"
          //must be stripped of
          sTmp = sTransactionParts[iI].replace("\"","");
          strParams.push_back ( sTmp.toStdString() );
        }
      }      
      
      //Convert the vector< std::string > into JSON with data types as defined in rpc/client.cpp: vRPCConvertParams[]
      params = RPCConvertValues("z_sign_offline", strParams);      
      
      oResult = tableRPC.execute("z_sign_offline", params);
      if (oResult.empty())
      {
        ui->teResult->setText("Transaction signing failed. The result is empty");
        return;
      }

      if (oResult[0].getType() != UniValue::VSTR)
      {
        ui->teResult->setText("Transaction signing failed. The result is empty");
        return;
      }

      std::string sMessage = oResult[0].get_str();
      if (sMessage.find("sendrawtransaction") == std::string::npos)
      {
        ui->teResult->setText( QString::fromStdString("Transaction signing failed. Could not find the signed transaction in the result: "+sMessage) );
        return;
      }
      ui->lbResult->setText("Signed transaction output: The transaction was successfully signed. Paste the contents in your on-line wallet to complete the transaction");
      ui->teResult->setText( QString::fromStdString(sMessage) );
    }
    catch (UniValue& objError)
    {
        try // Nice formatting for standard-format error
        {
            int code = find_value(objError, "code").get_int();
            std::string message = find_value(objError, "message").get_str();

            sMsg.asprintf("Transaction signing failed: %s\n",message.c_str());
            ui->teResult->setText(sMsg);
        }
        catch (const std::runtime_error&) // raised when converting to invalid type, i.e. missing code or message
        {
            ui->teResult->setText("Transaction signing failed. Could not parse the response. Please look in the log and command line output\n");
        }
    }
    catch (...)
    {
        ui->teResult->setText("Transaction signing failed. Could not parse the response. Please look in the log and command line output\n");
    }
}

void ZSignDialog::clear()
{
    //Hide the result frame
    ui->teInput->clear();
    ui->lbResult->setText("Signed transaction output:");
    ui->teResult->clear();    
}

void ZSignDialog::reject()
{
    clear();
}

void ZSignDialog::accept()
{
    clear();
}


void ZSignDialog::setResult(const string sHeading, const string sResult)
{
    if (sHeading.length() == 0)
    {
      ui->lbResult->setText("Result: ");
    }
    else
    {
      ui->lbResult->setText("Result:"+QString::fromStdString(sHeading));
    }

    ui->teResult->setText( QString::fromStdString(sResult) );

    ui->frameResult->show();
}


