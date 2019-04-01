#include "PSBT.h"

size_t PSBT::from_stream(ParseStream *s){
    if(status == PARSING_FAILED){
        return 0;
    }
    if(status == PARSING_DONE){
		// free memory
		if(tx.inputsNumber > 0){
			for(uint i=0; i<tx.inputsNumber; i++){
				if(txInsMeta[i].derivationsLen > 0){
					for(uint j=0; j<txInsMeta[i].derivationsLen; j++){
						if(txInsMeta[i].derivations[j].derivationLen > 0){
							delete [] txInsMeta[i].derivations[j].derivation;
						}
					}
					delete [] txInsMeta[i].derivations;
				}
				if(txInsMeta[i].signaturesLen > 0){
					delete [] txInsMeta[i].signatures;
				}
			}
			delete [] txInsMeta;
		}
		if(tx.outputsNumber > 0){
			for(uint i=0; i<tx.outputsNumber; i++){
				if(txOutsMeta[i].derivationsLen > 0){
					for(uint j=0; j<txOutsMeta[i].derivationsLen; j++){
						if(txOutsMeta[i].derivations[j].derivationLen > 0){
							delete [] txOutsMeta[i].derivations[j].derivation;
						}
					}
					delete [] txOutsMeta[i].derivations;
				}
			}
			delete [] txOutsMeta;
		}
        tx = Tx();
        bytes_parsed = 0;
        current_section = 0;
        last_key_pos = 5;
    }
    status = PARSING_INCOMPLETE;
    size_t bytes_read = 0;
    uint8_t prefix[] = {0x70, 0x73, 0x62, 0x74, 0xFF};
    while(s->available() && bytes_read+bytes_parsed < 5){
        uint8_t c = s->read();
        bytes_read++;
        if(c != prefix[bytes_read+bytes_parsed-1]){
            status = PARSING_FAILED;
            bytes_parsed += bytes_read;
            return bytes_read;
        }
    }
    // global scope
    if(bytes_read+bytes_parsed == last_key_pos){
    	bytes_read += s->parse(&key);
    	bytes_read += s->parse(&value);
    }
    while(s->available() && key.getStatus() == PARSING_INCOMPLETE){
    	bytes_read += s->parse(&key);
    }
    if(key.getStatus() == PARSING_FAILED){
        status = PARSING_FAILED;
        bytes_parsed += bytes_read;
        return bytes_read;
    }
    while(s->available() && value.getStatus() == PARSING_INCOMPLETE){
    	bytes_read += s->parse(&value);
    }
    if(value.getStatus() == PARSING_FAILED){
        status = PARSING_FAILED;
        bytes_parsed += bytes_read;
        return bytes_read;
    }
    if(last_key_pos == 5 && value.getStatus() == PARSING_DONE && key.getStatus() == PARSING_DONE){
    	uint8_t * arr = (uint8_t *)calloc(key.length(), sizeof(uint8_t));
    	key.serialize(arr, key.length());
    	if(key.length() != 2 || arr[0] != 1 || arr[1] != 0){
    		status = PARSING_FAILED;
    	}
    	free(arr);
    	if(status == PARSING_FAILED){
	        bytes_parsed += bytes_read;
	        return bytes_read;
    	}
    	arr = (uint8_t *)calloc(value.length(), sizeof(uint8_t));
    	value.serialize(arr, value.length());
    	size_t l = lenVarInt(value.length());
    	tx.parse(arr+l, value.length()-l);
    	if(tx.getStatus() != PARSING_DONE){
    		status = PARSING_FAILED;
    	}
    	free(arr);
    	if(status == PARSING_FAILED){
	        bytes_parsed += bytes_read;
	        return bytes_read;
    	}
		txInsMeta = new PSBTInputMetadata[tx.inputsNumber];
		for(uint i=0; i<tx.inputsNumber; i++){
			txInsMeta[i].derivationsLen = 0;
			txInsMeta[i].signaturesLen = 0;
		}
		txOutsMeta = new PSBTOutputMetadata[tx.outputsNumber];
		for(uint i=0; i<tx.outputsNumber; i++){
			txOutsMeta[i].derivationsLen = 0;
		}
    	last_key_pos += key.length()+value.length();
    }
    uint8_t sections_number = 0;
    if(last_key_pos > 5){ // tx is already parsed
    	sections_number = 1+tx.inputsNumber+tx.outputsNumber;
    }
    // parsing keys and values
    while(s->available() && current_section < sections_number){
    	if(key.getStatus() == PARSING_DONE && value.getStatus() == PARSING_DONE){
    		bytes_read += s->parse(&key);
    	}
    	if(key.getStatus() == PARSING_DONE){
    		if(key.length() == 1){ // delimiter
	    		current_section ++;
		    	last_key_pos += key.length();
		    	continue;
	    	}else{
	    		bytes_read += s->parse(&value);
	    	}
    	}
    	if(key.getStatus() == PARSING_FAILED || value.getStatus() == PARSING_FAILED){
    		status = PARSING_FAILED;
	        bytes_parsed += bytes_read;
	        return bytes_read;
    	}
    	if(key.getStatus() == PARSING_INCOMPLETE){
    		bytes_read += s->parse(&key);
    	}
    	if(value.getStatus() == PARSING_INCOMPLETE){
    		bytes_read += s->parse(&value);
    	}
    	if(key.getStatus() == PARSING_DONE && value.getStatus() == PARSING_DONE){
	    	int res = add(current_section, &key, &value);
	    	if(res < 0){
	    		status = PARSING_FAILED;
	    		bytes_parsed += bytes_read;
	    		return bytes_read;
	    	}
	    	last_key_pos += key.length() + value.length();
    	}
    }
    if(current_section == sections_number && sections_number > 0){
    	status = PARSING_DONE;
    	key = Script();
    	value = Script();
    }
    bytes_parsed += bytes_read;
	return bytes_read;
}

int PSBT::add(uint8_t section, const Script * k, const Script * v){
	if(section == 0 || section > 1+tx.inputsNumber+tx.outputsNumber){
		return 0;
	}
	uint8_t * key_arr = (uint8_t *)calloc(k->length(), sizeof(uint8_t));
	k->serialize(key_arr, k->length());
	uint8_t * val_arr = (uint8_t *)calloc(v->length(), sizeof(uint8_t));
	v->serialize(val_arr, v->length());
	uint8_t key_code = key_arr[lenVarInt(k->length())];
	int res = 0;

	if(section < 1+tx.inputsNumber){ // input section
		uint8_t input = section-1;
		switch(key_code){
			case 0: { // PSBT_IN_NON_WITNESS_UTXO
				// we need to verify that tx hashes to prevtx_hash
				// and get corresponding txOut from it. We don't need to keep the tx itself.
				if(k->length() != 2){
					res = -1;
					break;
				}
				Tx tempTx;
				tempTx.parse(val_arr+lenVarInt(v->length()), v->length()-lenVarInt(v->length()));
				if(tempTx.getStatus() != PARSING_DONE){
					res = -2;
					break;
				}
				uint8_t hash[32];
				tx.hash(hash);
				if(memcmp(hash, tx.txIns[input].hash, 32) != 0){
					res = -3;
					break;
				}
				if(tempTx.outputsNumber <= tx.txIns[input].outputIndex){
					res = -3;
					break;
				}
				txInsMeta[input].txOut = tempTx.txOuts[tx.txIns[input].outputIndex];
				res = 1;
				break;
			}
			case 1: { // PSBT_IN_WITNESS_UTXO
				if(k->length() != 2){
					res = -1;
					break;
				}
				txInsMeta[input].txOut.parse(val_arr+lenVarInt(v->length()), v->length()-lenVarInt(v->length()));
				if(txInsMeta[input].txOut.getStatus() != PARSING_DONE){
					res = -2;
					break;
				}
				res = 1;
				break;
			}
			case 2: { // PSBT_IN_PARTIAL_SIG
				if(k->length() != 35 && k->length() != 67){
					res = -1;
					break;
				}
				PSBTPartialSignature psig;
				psig.pubkey.parse(key_arr+2, k->length()-2);
				if(psig.pubkey.getStatus() != PARSING_DONE){
					res = -1;
					break;
				}
				psig.signature.parse(val_arr+1, v->length()-1);
				if(psig.signature.getStatus() != PARSING_DONE){
					res = -2;
					break;
				}
				if(txInsMeta[input].signaturesLen == 0){
					txInsMeta[input].signaturesLen = 1;
					txInsMeta[input].signatures = new PSBTPartialSignature[txInsMeta[input].signaturesLen];
				}else{
					PSBTPartialSignature * p = txInsMeta[input].signatures;
					txInsMeta[input].signaturesLen ++;
					txInsMeta[input].signatures = new PSBTPartialSignature[txInsMeta[input].signaturesLen];
					for(uint i=0; i<txInsMeta[input].signaturesLen-1; i++){
						txInsMeta[input].signatures[i] = p[i];
					}
					delete [] p;
				}
				txInsMeta[input].signatures[txInsMeta[input].signaturesLen-1] = psig;
				res = 1;
				break;
			}
			case 3: { // PSBT_IN_SIGHASH_TYPE
				// not implemented
				break;
			}
			case 4: { // PSBT_IN_REDEEM_SCRIPT
				if(k->length() != 2){
					res = -1;
					break;
				}
				txInsMeta[input].redeemScript.parse(val_arr, v->length());
				res = 1;
				break;
			}
			case 5: { // PSBT_IN_WITNESS_SCRIPT
				if(k->length() != 2){
					res = -1;
					break;
				}
				txInsMeta[input].witnessScript.parse(val_arr, v->length());
				res = 1;
				break;
			}
			case 6: { // PSBT_IN_BIP32_DERIVATION
				// TODO: move to function
				if(k->length() != 35 && k->length() != 67){
					res = -1;
					break;
				}
				PSBTDerivation der;
				der.pubkey.parse(key_arr+2, k->length()-2);
				if(der.pubkey.getStatus() != PARSING_DONE){
					res = -1;
					break;
				}
				memcpy(der.fingerprint, val_arr+lenVarInt(v->length()), 4);
				der.derivationLen = (v->length()-lenVarInt(v->length())-4)/sizeof(uint32_t);
				der.derivation = (uint32_t *)calloc(der.derivationLen, sizeof(uint32_t));
				for(uint i=0; i<der.derivationLen; i++){
					der.derivation[i] = littleEndianToInt(val_arr+lenVarInt(v->length())+4*(i+1),4);
				}
				if(txInsMeta[input].derivationsLen == 0){
					txInsMeta[input].derivationsLen = 1;
					txInsMeta[input].derivations = new PSBTDerivation[txInsMeta[input].derivationsLen];
				}else{
					PSBTDerivation * p = txInsMeta[input].derivations;
					txInsMeta[input].derivationsLen ++;
					txInsMeta[input].derivations = new PSBTDerivation[txInsMeta[input].derivationsLen];
					for(uint i=0; i<txInsMeta[input].derivationsLen-1; i++){
						txInsMeta[input].derivations[i] = p[i];
					}
					delete [] p;
				}
				txInsMeta[input].derivations[txInsMeta[input].derivationsLen-1] = der;
				res = 1;
				break;
			}
			case 7: { // PSBT_IN_FINAL_SCRIPTSIG
				// not implemented
				break;
			}
			case 8: { // PSBT_IN_FINAL_SCRIPTWITNESS
				// not implemented
				break;
			}
		}
	}else{ // output section
		uint8_t output = section-1-tx.inputsNumber;
		switch(key_code){
			case 0: { // PSBT_OUT_REDEEM_SCRIPT
				if(k->length() != 2){
					return -1;
				}
				txOutsMeta[output].redeemScript.parse(val_arr, v->length());
				res = 1;
				break;
			}
			case 1: { // PSBT_OUT_WITNESS_SCRIPT
				if(k->length() != 2){
					return -1;
				}
				txOutsMeta[output].witnessScript.parse(val_arr, v->length());
				res = 1;
				break;
			}
			case 2: { // PSBT_OUT_BIP32_DERIVATION 
				// TODO: move to function
				if(k->length() != 35 && k->length() != 67){
					res = -1;
					break;
				}
				PSBTDerivation der;
				der.pubkey.parse(key_arr+2, k->length()-2);
				if(der.pubkey.getStatus() != PARSING_DONE){
					res = -1;
					break;
				}
				memcpy(der.fingerprint, val_arr+lenVarInt(v->length()), 4);
				der.derivationLen = (v->length()-lenVarInt(v->length())-4)/sizeof(uint32_t);
				der.derivation = (uint32_t *)calloc(der.derivationLen, sizeof(uint32_t));
				for(uint i=0; i<der.derivationLen; i++){
					der.derivation[i] = littleEndianToInt(val_arr+lenVarInt(v->length())+4*(i+1),4);
				}
				if(txOutsMeta[output].derivationsLen == 0){
					txOutsMeta[output].derivationsLen = 1;
					txOutsMeta[output].derivations = new PSBTDerivation[txOutsMeta[output].derivationsLen];
				}else{
					PSBTDerivation * p = txOutsMeta[output].derivations;
					txOutsMeta[output].derivationsLen ++;
					txOutsMeta[output].derivations = new PSBTDerivation[txOutsMeta[output].derivationsLen];
					for(uint i=0; i<txOutsMeta[output].derivationsLen-1; i++){
						txOutsMeta[output].derivations[i] = p[i];
					}
					delete [] p;
				}
				txOutsMeta[output].derivations[txOutsMeta[output].derivationsLen-1] = der;
				res = 1;
				break;
			}
		}
	}
	free(key_arr);
	free(val_arr);
	return res; // by default - ignore the key-value pair
}

size_t PSBT::to_stream(SerializeStream *s, size_t offset) const{
	return s->serialize(&tx, offset);
}

size_t PSBT::length() const{
	return tx.length();
}

PSBT::PSBT(PSBT const &other){
	tx = other.tx;
	status = other.status;
	txInsMeta = new PSBTInputMetadata[tx.inputsNumber];
	txOutsMeta = new PSBTOutputMetadata[tx.outputsNumber];
	for(uint i=0; i<tx.inputsNumber; i++){
		txInsMeta[i] = other.txInsMeta[i];
		txInsMeta[i].derivations = new PSBTDerivation[txInsMeta[i].derivationsLen];
		for(uint j=0; j<txInsMeta[i].derivationsLen; j++){
			txInsMeta[i].derivations[j] = other.txInsMeta[i].derivations[j];
			txInsMeta[i].derivations[j].derivation = new uint32_t[txInsMeta[i].derivations[j].derivationLen];
			memcpy(txInsMeta[i].derivations[j].derivation, other.txInsMeta[i].derivations[j].derivation, txInsMeta[i].derivations[j].derivationLen*sizeof(uint32_t));
		}
		txInsMeta[i].signatures = new PSBTPartialSignature[txInsMeta[i].signaturesLen];
		for(uint j=0; j<txInsMeta[i].signaturesLen; j++){
			txInsMeta[i].signatures[j] = other.txInsMeta[i].signatures[j];
		}
	}
	for(uint i=0; i<tx.outputsNumber; i++){
		txOutsMeta[i] = other.txOutsMeta[i];
		txOutsMeta[i].derivations = new PSBTDerivation[txOutsMeta[i].derivationsLen];
		for(uint j=0; j<txOutsMeta[i].derivationsLen; j++){
			txOutsMeta[i].derivations[j] = other.txOutsMeta[i].derivations[j];
			txOutsMeta[i].derivations[j].derivation = new uint32_t[txOutsMeta[i].derivations[j].derivationLen];
			memcpy(txOutsMeta[i].derivations[j].derivation, other.txOutsMeta[i].derivations[j].derivation, txOutsMeta[i].derivations[j].derivationLen*sizeof(uint32_t));
		}
	}
}

PSBT::~PSBT(){
	// free memory
	if(tx.inputsNumber > 0){
		for(uint i=0; i<tx.inputsNumber; i++){
			if(txInsMeta[i].derivationsLen > 0){
				for(uint j=0; j<txInsMeta[i].derivationsLen; j++){
					if(txInsMeta[i].derivations[j].derivationLen > 0){
						delete [] txInsMeta[i].derivations[j].derivation;
					}
				}
				delete [] txInsMeta[i].derivations;
			}
			if(txInsMeta[i].signaturesLen > 0){
				delete [] txInsMeta[i].signatures;
			}
		}
		delete [] txInsMeta;
	}
	if(tx.outputsNumber > 0){
		for(uint i=0; i<tx.outputsNumber; i++){
			if(txOutsMeta[i].derivationsLen > 0){
				for(uint j=0; j<txOutsMeta[i].derivationsLen; j++){
					if(txOutsMeta[i].derivations[j].derivationLen > 0){
						delete [] txOutsMeta[i].derivations[j].derivation;
					}
				}
				delete [] txOutsMeta[i].derivations;
			}
		}
		delete [] txOutsMeta;
	}
}

uint8_t PSBT::sign(const HDPrivateKey root){
	uint8_t fingerprint[4];
	root.fingerprint(fingerprint);
	uint8_t counter = 0;
	// in most cases only one account key is required, so we can cache it
	uint32_t * first_derivation = NULL;
	uint8_t first_derivation_len = 0;
	HDPrivateKey account;
	for(uint i=0; i<tx.inputsNumber; i++){
		if(txInsMeta[i].derivationsLen > 0){
			for(uint j=0; j<txInsMeta[i].derivationsLen; j++){
				if(memcmp(fingerprint, txInsMeta[i].derivations[j].fingerprint, 4) == 0){
					// caching account key here
					if(first_derivation == NULL){
						first_derivation = txInsMeta[i].derivations[j].derivation; 
						first_derivation_len = 0;
						for(uint k=0; k < txInsMeta[i].derivations[j].derivationLen; k++){
							if(txInsMeta[i].derivations[j].derivation[k] >= 0x80000000){
								first_derivation_len++;
							}else{
								break;
							}
						}
						account = root.derive(first_derivation, first_derivation_len);
					}
					PrivateKey pk;
					// checking if cached key is ok
					if(memcmp(first_derivation, txInsMeta[i].derivations[j].derivation, first_derivation_len*sizeof(uint32_t))==0){
						pk = account.derive(txInsMeta[i].derivations[j].derivation+first_derivation_len, txInsMeta[i].derivations[j].derivationLen - first_derivation_len);
					}else{
						pk = root.derive(txInsMeta[i].derivations[j].derivation, txInsMeta[i].derivations[j].derivationLen);
					}
					if(txInsMeta[i].derivations[j].pubkey == pk.publicKey()){
						// can sign - let's sign
						uint8_t h[32];
						if(txInsMeta[i].witnessScript.length() > 1){ // P2WSH / P2SH_P2WSH
						    tx.sigHashSegwit(h, i, txInsMeta[i].witnessScript, txInsMeta[i].txOut.amount);
						}else{
							if(txInsMeta[i].redeemScript.length() > 1){
								if(txInsMeta[i].redeemScript.type() == P2WPKH){ // P2SH_P2WPKH
								    tx.sigHashSegwit(h, i, txInsMeta[i].redeemScript, txInsMeta[i].txOut.amount);
								}else{ // P2SH
								    tx.sigHash(h, i, txInsMeta[i].redeemScript);
								}
							}else{ // P2WPKH / P2PKH / DIRECT_SCRIPT
								if(txInsMeta[i].txOut.scriptPubkey.type() == P2WPKH){
								    tx.sigHashSegwit(h, i, txInsMeta[i].txOut.scriptPubkey, txInsMeta[i].txOut.amount);
								}else{ // P2PKH / DIRECT_SCRIPT
								    tx.sigHash(h, i, txInsMeta[i].txOut.scriptPubkey);
								}
							}
						}
						Signature sig = pk.sign(h);

						// adding partial signature to the PSBT
						uint8_t arr[67];
						arr[1] = 0x02; // PSBT_IN_PARTIAL_SIG
						uint8_t len = 1 + pk.publicKey().serialize(arr+2, 65);
						arr[0] = len;
						Script key;
						key.parse(arr, len+1);

						Script value;
						value.push(sig);
						add(i+1, &key, &value);
						counter++; // can sign
					}
				}
			}
		}
	}
	return counter;
}

uint64_t PSBT::fee() const{
	uint64_t input_amount = 0;
	uint64_t output_amount = 0;
	for(uint i=0; i<tx.inputsNumber; i++){
		if(txInsMeta[i].txOut.amount == 0){
			return 0;
		}
		input_amount += txInsMeta[i].txOut.amount;
	}
	for(uint i=0; i<tx.outputsNumber; i++){
		output_amount += tx.txOuts[i].amount;
	}
	if(output_amount > input_amount){
		return 0;
	}
	return input_amount-output_amount;
}

PSBT& PSBT::operator=(PSBT const &other){
	// free memory
	if(tx.inputsNumber > 0){
		for(uint i=0; i<tx.inputsNumber; i++){
			if(txInsMeta[i].derivationsLen > 0){
				for(uint j=0; j<txInsMeta[i].derivationsLen; j++){
					if(txInsMeta[i].derivations[j].derivationLen > 0){
						delete [] txInsMeta[i].derivations[j].derivation;
					}
				}
				delete [] txInsMeta[i].derivations;
			}
			if(txInsMeta[i].signaturesLen > 0){
				delete [] txInsMeta[i].signatures;
			}
		}
		delete [] txInsMeta;
	}
	if(tx.outputsNumber > 0){
		for(uint i=0; i<tx.outputsNumber; i++){
			if(txOutsMeta[i].derivationsLen > 0){
				for(uint j=0; j<txOutsMeta[i].derivationsLen; j++){
					if(txOutsMeta[i].derivations[j].derivationLen > 0){
						delete [] txOutsMeta[i].derivations[j].derivation;
					}
				}
				delete [] txOutsMeta[i].derivations;
			}
		}
		delete [] txOutsMeta;
	}
	// copy
	tx = other.tx;
	status = other.status;
	if(tx.inputsNumber > 0){
		txInsMeta = new PSBTInputMetadata[tx.inputsNumber];
		for(uint i=0; i<tx.inputsNumber; i++){
			txInsMeta[i] = other.txInsMeta[i];
			if(txInsMeta[i].derivationsLen > 0){
				txInsMeta[i].derivations = new PSBTDerivation[txInsMeta[i].derivationsLen];
				for(uint j=0; j<txInsMeta[i].derivationsLen; j++){
					txInsMeta[i].derivations[j] = other.txInsMeta[i].derivations[j];
					txInsMeta[i].derivations[j].derivation = new uint32_t[txInsMeta[i].derivations[j].derivationLen];
					memcpy(txInsMeta[i].derivations[j].derivation, other.txInsMeta[i].derivations[j].derivation, txInsMeta[i].derivations[j].derivationLen*sizeof(uint32_t));
				}
			}
			if(txInsMeta[i].signaturesLen > 0){
				txInsMeta[i].signatures = new PSBTPartialSignature[txInsMeta[i].signaturesLen];
				for(uint j=0; j<txInsMeta[i].signaturesLen; j++){
					txInsMeta[i].signatures[j] = other.txInsMeta[i].signatures[j];
				}
			}
		}
	}
	if(tx.outputsNumber > 0){
		txOutsMeta = new PSBTOutputMetadata[tx.outputsNumber];
		for(uint i=0; i<tx.outputsNumber; i++){
			txOutsMeta[i] = other.txOutsMeta[i];
			txOutsMeta[i].derivations = new PSBTDerivation[txOutsMeta[i].derivationsLen];
			for(uint j=0; j<txOutsMeta[i].derivationsLen; j++){
				txOutsMeta[i].derivations[j] = other.txOutsMeta[i].derivations[j];
				txOutsMeta[i].derivations[j].derivation = new uint32_t[txOutsMeta[i].derivations[j].derivationLen];
				memcpy(txOutsMeta[i].derivations[j].derivation, other.txOutsMeta[i].derivations[j].derivation, txOutsMeta[i].derivations[j].derivationLen*sizeof(uint32_t));
			}
		}
	}
	return *this;
}
