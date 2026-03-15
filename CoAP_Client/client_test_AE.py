import asyncio
from aiocoap import Message, Context, Code
from aiocoap.numbers.types import Type as MessageType
from define_ip import IP_ADDRESS 


async def send_request(protocol, request, operation, expected):
    try:
        print(f"\nSending {operation}")
        print(f"Expected: {expected}")
        response = await protocol.request(request).response
        print(f'Response Type: {response.mtype}')
        print(f'Response Code: {response.code}')
        if response.payload:
            print(f'Response Payload: {response.payload.decode("utf-8")}')
        return response
    except Exception as e:
        print(f'Failed to {operation}:')
        print(e)
        return None

async def main():
    protocol = await Context.create_client_context()

######################-POST-##################################
    payload = b'''
    {
        "m2m:ae": {
            "ty": 2, 
            "ri": "CmBjk-Lu",
            "rn": "water",
            "pi": "mn-nameID",
            "ct": "2025-06-01 15:18:29",
            "lt": "2025-06-01 15:18:29",
            "api": "NO1.com.water.company",
            "aei": "CmBjk-Lu",
            "rr": 1,
            "et": "2025-06-30 21:42:45",
            "lbl": [
            "a",
            "b",
            "key1",
            "key2"
            ],
            "poa": [
            "http://192.168.1.163:8080"
            ],
            "srv": [
            "3"
            ]
        }
    }'''
    request_post = Message(
        mtype=MessageType.CON,
        code=Code.POST,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name',
        payload=payload        
    )
    request_post.opt.content_format = 50    

######################-GET-##################################
    request_get = Message(mtype=MessageType.CON, code=Code.GET, uri=f'coap://{IP_ADDRESS}:5683/mn-name/water')
    request_get_csebase = Message(mtype=MessageType.CON, code=Code.GET, uri=f'coap://{IP_ADDRESS}:5683/mn-name/')
    request_get_no_csebase = Message(mtype=MessageType.CON, code=Code.GET, uri=f'coap://{IP_ADDRESS}:5683/mn-fake/')
######################-PUT-##################################
    payload = b'''
    {
        "m2m:ae": {
            "et": "2025-08-30 21:42:45"
        }
    }'''

    request_put = Message(
        code=Code.PUT,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water',
        payload=payload,
        mtype=MessageType.CON
    )
    request_put.opt.content_format = 50

    payload2 = b'''
    {
        "m2m:ae": {
            "et": "2025-01-30 21:42:45"
        }
    }'''

    request_put_bad_et = Message(
        code=Code.PUT,
        uri=f'coap://{IP_ADDRESS}:5683/mn-name/water',
        payload=payload2,
        mtype=MessageType.CON
    )
    request_put_bad_et.opt.content_format = 50

######################-DELETE-##################################
    request_delete = Message(code=Code.DELETE,uri=f'coap://{IP_ADDRESS}:5683/mn-name/water', mtype=MessageType.CON)   

######################-SEND REQUESTs-##################################
    requests = [
            (request_get_csebase, "GET CSEBase", 205),
            (request_get_no_csebase, "GET CSEBase Inexistente", 404),
            (request_delete, "DELETE", 202),
            (request_get, "GET_AE_Inexistente", 404),
            (request_put, "PUT_AE_Inexistente", 404),
            (request_delete, "DELETE_AE_Inexistente", 404),
            (request_post, "POST", 201),
            (request_post, "POST_Repetido", 422),
            (request_get, "GET", 205),
            (request_put, "PUT", 205),
            (request_put_bad_et, "PUT_bad_et", 422),
    ]

    # Modified for loop to handle all three values
    for request, operation, expected in requests:
        response = await send_request(protocol, request, operation, expected)
        await asyncio.sleep(1)

if __name__ == "__main__":
    asyncio.run(main())